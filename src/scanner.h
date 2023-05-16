/*
 * scanner.h -- fast lexical analyzer for (DNS) zone files
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef SCANNER_H
#define SCANNER_H

#include <assert.h>
#include <string.h>
#include <stdio.h>

// Copied from simdjson under the terms of The BSD-3-Clause license.
// Copyright (c) 2018-2023 The simdjson authors
static inline uint64_t find_escaped(
  uint64_t backslash, uint64_t *is_escaped)
{
  backslash &= ~ *is_escaped;

  uint64_t follows_escape = backslash << 1 | *is_escaped;

  // Get sequences starting on even bits by clearing out the odd series using +
  const uint64_t even_bits = 0x5555555555555555ULL;
  uint64_t odd_sequence_starts = backslash & ~even_bits & ~follows_escape;
  uint64_t sequences_starting_on_even_bits;
  *is_escaped = add_overflow(odd_sequence_starts, backslash, &sequences_starting_on_even_bits);
  uint64_t invert_mask = sequences_starting_on_even_bits << 1; // The mask we want to return is the *escaped* bits, not escapes.

  // Mask every other backslashed character as an escaped character
  // Flip the mask for sequences that start on even bits, to correct them
  return (even_bits ^ invert_mask) & follows_escape;
}

// special characters in zone files cannot be identified without branching
// (unlike json) due to comments (*). no algorithm was found (so far) that
// can correctly identify quoted and comment regions where a quoted region
// includes a semicolon (or newline for that matter) and/or a comment region
// includes one (or more) quote characters. also, for comments, only newlines
// directly following a non-escaped, non-quoted semicolon must be included
static inline void find_delimiters(
  uint64_t quotes,
  uint64_t semicolons,
  uint64_t newlines,
  uint64_t in_quoted,
  uint64_t in_comment,
  uint64_t *quoted,
  uint64_t *comment)
{
  uint64_t delimiters, starts = quotes | semicolons;
  uint64_t end;

  assert(!(quotes & semicolons));

  // carry over state from previous block
  end = (newlines & in_comment) | (quotes & in_quoted);
  end &= -end;

  delimiters = end;
  starts &= ~((in_comment | in_quoted) ^ (-end - end));

  while (starts) {
    const uint64_t start = -starts & starts;
    assert(start);
    const uint64_t quote = quotes & start;
    const uint64_t semicolon = semicolons & start;

    // FIXME: technically, this introduces a data dependency
    end = (newlines & -semicolon) | (quotes & (-quote - quote));
    end &= -end;

    delimiters |= end | start;
    starts &= -end - end;
  }

  *quoted = delimiters & quotes;
  *comment = delimiters & ~quotes;
}

static inline uint64_t follows(const uint64_t match, uint64_t *overflow)
{
  const uint64_t result = match << 1 | (*overflow);
  *overflow = match >> 63;
  return result;
}

typedef struct block block_t;
struct block {
  simd_8x64_t input;
  uint64_t newline;
  uint64_t backslash;
  uint64_t escaped;
  uint64_t comment;
  uint64_t quoted;
  uint64_t semicolon;
  uint64_t in_quoted;
  uint64_t in_comment;
  uint64_t contiguous;
  uint64_t follows_contiguous;
  uint64_t blank;
  uint64_t special;
  uint64_t bits;
};

static zone_inline void scan(zone_parser_t *parser, block_t *block)
{
  // escaped newlines are classified as contiguous. however, escape sequences
  // have no meaning in comments and newlines, escaped or not, have no
  // special meaning in quoted
  block->newline = simd_find_8x64(&block->input, '\n');
  block->backslash = simd_find_8x64(&block->input, '\\');
  block->escaped = find_escaped(
    block->backslash, &parser->file->indexer.is_escaped);

  block->comment = 0;
  block->quoted = simd_find_8x64(&block->input, '"') & ~block->escaped;
  block->semicolon = simd_find_8x64(&block->input, ';') & ~block->escaped;

  block->in_quoted = parser->file->indexer.in_quoted;
  block->in_comment = parser->file->indexer.in_comment;

  if (block->in_comment || block->semicolon) {
    find_delimiters(
      block->quoted,
      block->semicolon,
      block->newline,
      block->in_quoted,
      block->in_comment,
     &block->quoted,
     &block->comment);

    block->in_quoted ^= prefix_xor(block->quoted);
    parser->file->indexer.in_quoted = (uint64_t)((int64_t)block->in_quoted >> 63);
    block->in_comment ^= prefix_xor(block->comment);
    parser->file->indexer.in_comment = (uint64_t)((int64_t)block->in_comment >> 63);
  } else {
    block->in_quoted ^= prefix_xor(block->quoted);
    parser->file->indexer.in_quoted = (uint64_t)((int64_t)block->in_quoted >> 63);
  }

  block->blank = simd_find_any_8x64(&block->input, delimiters[CONTIGUOUS].blank) &
                   ~(block->escaped | block->in_quoted | block->in_comment);
  block->special = simd_find_any_8x64(&block->input, delimiters[CONTIGUOUS].special) &
                     ~(block->escaped | block->in_quoted | block->in_comment);

  block->contiguous =
    ~(block->blank | block->special | block->quoted) & ~(block->in_quoted | block->in_comment);
  block->follows_contiguous =
    follows(block->contiguous, &parser->file->indexer.follows_contiguous);

  // quoted and contiguous have dynamic lengths, write two indexes
  block->bits = (block->contiguous & ~block->follows_contiguous) | (block->quoted & block->in_quoted) | block->special;
}

static int32_t refill(zone_parser_t *parser)
{
  zone_file_t *file = parser->file;

  // grow buffer if necessary
  if (file->buffer.length == file->buffer.size) {
    size_t size = file->buffer.size + ZONE_WINDOW_SIZE;
    char *data = file->buffer.data;
    if (!(data = realloc(data, size + 1)))
      OUT_OF_MEMORY(parser);
    file->buffer.size = size;
    file->buffer.data = data;
  }

  size_t count = fread(file->buffer.data + file->buffer.length,
                       sizeof(file->buffer.data[0]),
                       file->buffer.size - file->buffer.length,
                       file->handle);

  if (count == 0 && ferror(file->handle))
    SYNTAX_ERROR(parser, "actually a read error");

  // always null-terminate so terminating token can point to something
  file->buffer.length += (size_t)count;
  file->buffer.data[file->buffer.length] = '\0';
  file->end_of_file = feof(file->handle) != 0;
  return 0;
}

static void tokenize(zone_parser_t *parser, const block_t *block)
{
  uint64_t bits = block->bits;
  uint64_t count = count_ones(bits);
  const char *base = parser->file->buffer.data + parser->file->buffer.index;

  // slow path if line feeds appear(ed) in strings
  if (zone_unlikely((parser->file->indexer.lines) ||
                    (block->newline & (block->contiguous | block->in_quoted))))
  {
    uint64_t newline = block->newline;
    for (uint64_t i=0; i < count; i++) {
      const uint64_t bit = -bits & bits;
      bits ^= bit;
      if (bit & newline) {
        parser->file->indexer.tail[i].data = line_feed;
        parser->file->indexer.tail[i].lines = parser->file->indexer.lines;
        parser->file->indexer.lines = 0;
        newline &= -bit;
      } else {
        // count newlines here so number of newlines remains correct if last
        // token is start of contiguous or quoted and index must be reset
        parser->file->indexer.tail[i].data = base + trailing_zeroes(bit);
        parser->file->indexer.lines += count_ones(newline & ~(-bit));
        newline &= -bit;
      }
    }

    parser->file->indexer.tail += count;
  } else {
    for (uint64_t i=0; i < ZONE_BLOCK_INDEXES; i++) {
      parser->file->indexer.tail[i].data = base + trailing_zeroes(bits);
      bits = clear_lowest_bit(bits);
    }

    if (zone_unlikely(count > ZONE_BLOCK_INDEXES)) {
      for (uint64_t i=ZONE_BLOCK_INDEXES; i < (2 * ZONE_BLOCK_INDEXES); i++) {
        parser->file->indexer.tail[i].data = base + trailing_zeroes(bits);
        bits = clear_lowest_bit(bits);
      }

      if (zone_unlikely(count > (2 * ZONE_BLOCK_INDEXES))) {
        for (uint64_t i=(2 * ZONE_BLOCK_INDEXES); i < count; i++) {
          parser->file->indexer.tail[i].data = base + trailing_zeroes(bits);
          bits = clear_lowest_bit(bits);
        }
      }
    }

    parser->file->indexer.tail += count;
  }
}

zone_nonnull_all()
void zone_close_file(zone_parser_t *parser, zone_file_t *file);

zone_nonnull_all()
static zone_no_inline int32_t step(zone_parser_t *parser, token_t *token)
{
  block_t block = { 0 };
  zone_file_t *file = parser->file;
  const char *start, *end;
  bool start_of_line = false;

  // start of line is initially always true
  if (file->indexer.tail == file->indexer.tape)
    start_of_line = true;
  else if (*(end = file->indexer.tail[-1].data) == '\n')
    start_of_line = (file->buffer.data + file->buffer.index) - end == 1;

  assert(*file->indexer.tail[0].data == '\0');
  file->indexer.tape[0] = file->indexer.tail[1];
  file->indexer.head = file->indexer.tape;
  file->indexer.tail = &file->indexer.tape[ !!file->indexer.tail[1].data ];

shuffle:
  if (file->end_of_file == ZONE_HAVE_DATA) {
    if (file->indexer.tape[0].data)
      start = file->indexer.tape[0].data;
    else
      start = file->buffer.data + file->buffer.index;
    file->indexer.head[0].data = file->buffer.data;
    const size_t length = (file->buffer.data + file->buffer.length) - start;
    memmove(file->buffer.data, start, length);
    file->buffer.length = length;
    file->buffer.data[length] = '\0';
    file->buffer.index = (file->buffer.data + file->buffer.index) - start;
    refill(parser);
  }

  start = file->buffer.data + file->buffer.index;

  while (file->buffer.length - file->buffer.index >= ZONE_BLOCK_SIZE) {
    if ((file->indexer.tape + ZONE_TAPE_SIZE) - file->indexer.tail < ZONE_BLOCK_SIZE)
      goto terminate;
    simd_loadu_8x64(&block.input, (uint8_t *)&file->buffer.data[file->buffer.index]);
    scan(parser, &block);
    tokenize(parser, &block);
    file->buffer.index += ZONE_BLOCK_SIZE;
  }

  size_t length = file->buffer.length - file->buffer.index;
  assert(length <= ZONE_BLOCK_SIZE);
  if (file->end_of_file == ZONE_HAVE_DATA)
    goto terminate;
  if (length > (size_t)((file->indexer.tape + ZONE_TAPE_SIZE) - file->indexer.tail))
    goto terminate;

  uint8_t buffer[ZONE_BLOCK_SIZE] = { 0 };
  memcpy(buffer, &file->buffer.data[file->buffer.index], length);
  const uint64_t clear = ~((1llu << length) - 1);
  simd_loadu_8x64(&block.input, buffer);
  scan(parser, &block);
  block.bits &= ~clear;
  block.contiguous &= ~clear;
  tokenize(parser, &block);
  file->buffer.index += length;
  file->end_of_file = ZONE_NO_MORE_DATA;

terminate:
  // make sure tape contains no partial tokens
  if ((int64_t)(block.contiguous | block.in_quoted) >> 63) {
    assert(file->indexer.tail > file->indexer.tape);
    file->indexer.tail[0] = file->indexer.tail[-1];
    file->indexer.tail--;
    assert(file->indexer.tail->data != line_feed);
  } else {
    file->indexer.tail[1] = (zone_index_t){ NULL };
  }

  file->indexer.tail[0].data = file->buffer.data + file->buffer.length;
  file->start_of_line = file->indexer.head[0].data == start && start_of_line;

  for (;;) {
    token->data = file->indexer.head[0].data;

    switch (*token->data) {
      case '\0':
        if (file->end_of_file != ZONE_NO_MORE_DATA)
          goto shuffle;
        if (file->grouped)
          SYNTAX_ERROR(parser, "Missing closing brace");
        assert(token->data == file->buffer.data + file->buffer.length);
        if (!file->includer)
          return token->code = END_OF_FILE;
        parser->file = file = file->includer;
        parser->owner = &file->owner;
        zone_close_file(parser, file);
        break;
      case '\n':
        if (zone_unlikely(token->data == line_feed))
          file->line += file->indexer.head[0].lines;
        file->line++;
        file->indexer.head++;
        if (file->grouped)
          break;
        file->start_of_line = classify[ (uint8_t)*(token->data+1) ] != BLANK;
        return token->code = LINE_FEED;
      case '\"':
        file->indexer.head++;
        return token->code = QUOTED;
      case '(':
        if (file->grouped)
          SYNTAX_ERROR(parser, "Nested opening brace");
        file->grouped = true;
        file->indexer.head++;
        break;
      case ')':
        if (!file->grouped)
          SYNTAX_ERROR(parser, "Missing opening brace");
        file->grouped = false;
        file->indexer.head++;
        break;
      default:
        file->indexer.head++;
        return token->code = CONTIGUOUS;
    }
  }
}

#endif // SCANNER_H
