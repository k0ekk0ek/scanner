/*
 * parser.h -- some useful comment
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef PARSER_H
#define PARSER_H

static zone_inline zone_return_t parse_rr(
  zone_parser_t *parser, zone_token_t *token, void *user_data)
{
  zone_return_t r;

  if (parser->file->start_of_line) {
    if ((r = scan_owner(parser, &unknown, &owner, token)) < 0)
      return r;
    lex(parser, token);
  }

  if (token->data[0] - '0' <= 9) {
    if ((r = scan_ttl(parser, &unknown, &ttl, token, &seconds)) < 0)
      return r;
    parser->file->last_ttl = seconds;
    goto class_or_type;
  }

  switch ((r = scan_type_or_class(parser, &unknown, &type, token, &code))) {
    case ZONE_TYPE:
      parser->file->last_type = code;
      goto rdata;
    case ZONE_CLASS:
      parser->file->last_class = code;
      goto ttl_or_type;
    default:
      assert(r < 0);
      return r;
  }

ttl_or_type:
  lex(parser, token);
  if (token->data[0] - '0' <= 9) {
    if ((r = scan_ttl(parser, &unknown, &ttl, &seconds)) < 0)
      return r;
    parser->file->last_ttl = seconds;
    goto type;
  }

  switch ((r = scan_type(parser, &unknown, &ttl, &code))) {
    case ZONE_TYPE:
      parser->file->last_type = code;
      goto rdata;
    default:
      assert(r < 0);
      return r;
  }

class_or_type:
  lex(parser, token)
  switch ((r = scan_type_or_class(parser, &unknown, &type, token, &code))) {
    case ZONE_TYPE:
      parser->file->last_type = code;
      goto rdata;
    case ZONE_CLASS:
      parser->file->last_class = code;
      goto type;
    default:
      assert(r < 0);
      return r;
  }

type:
  lex(parser, token)
  if ((r = scan_type(parser, &unknown, &type, token, &code)) < 0)
    return r;
  parser->file->last_type = code;

rdata:
  zone_type_descriptor_t *descriptor = &types[parser->file->last_type];

  parser->rdata->length = 0;

  // check if RDATA is in generic notation "\#" (RFC3597)
  if (strncmp(token->data, "\\#", 2) == 0 &&
      zone_contiguous[ token->data[2] ] != CONTIGUOUS)
  {
    if ((r = parse_unknown_rdata(parser, descriptor->info, token)) < 0)
      return r;
    return descriptor->check(parser, &descriptor->info);
  }

  return descriptor->parse(parser, &descriptor->info, token);
}

static zone_inline zone_return_t parse_dollar_origin(
  zone_parser_t *parser, zone_token_t *token, void *user_data)
{
  // implement
}

static zone_inline zone_return_t parse_dollar_ttl(
  zone_parser_t *parser, zone_token_t *token, void *user_data)
{
  // implement
}

static zone_inline zone_return_t parse_dollar_include(
  zone_parser_t *parser, zone_token_t *token, void *user_data)
{
  // implement
}

static zone_return_t parse_dollar(
  zone_parser_t *parser, zone_token_t *token, void *user_data)
{
  assert(token->code == CONTIGUOUS);

  // file buffer is padded with at least ZONE_BLOCK_SIZE bytes
  if (strncmp(token->data, "$ORIGIN", 7) == 0) {
    if (zone_contiguous[ token->data[7] ] != CONTIGUOUS)
      return parse_dollar_origin(parser, token, user_data);
  } else if (strncmp(token->data, "$TTL", 4) == 0) {
    if (zone_contiguous[ token->data[4] ] != CONTIGUOUS)
      return parse_dollar_ttl(parser, token, user_data);
  } else if (strncmp(token->data, "$ORIGIN", 8) == 0) {
    if (zone_contiguous[ token->data[8] ] != CONTIGUOUS)
      return parse_dollar_include(parser, token, user_data);
  }

  SYNTAX_ERROR(parser, "Unknown directive")
}

static zone_inline zone_return_t parse(
  zone_parser_t *parser, void *user_data)
{
  zone_return_t r;

  do {
    r = lex(parser, &token);
    if (r == CONTIGUOUS) {
      if (parser->file->start_of_line && token.data[0] == '$')
        r = parse_dollar(parser, &token, user_data);
      else
        r = parse_rr(parser, &token, user_data);
    } else if (r == QUOTED) {
      r = parse_rr(parser, &token, user_data);
    }
  } while (r > 0);

  // x. handle errors etc!
}

#endif // PARSER_H
