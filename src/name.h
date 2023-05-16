/*
 * name.h -- some useful comment
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef NAME_H
#define NAME_H

struct name_block {
  size_t length;
  uint64_t escape_bits;
  uint64_t label_bits;
};

zone_nonnull_all()
static zone_inline copy_name_block(
  name_block_t *block,
  simd_table_t blank,
  simd_table_t special,
  const char *text,
  uint8_t *wire)
{
  simd_8x_t input;

  simd_loadu_8x(&input, text);
  simd_storeu_8x(wire, &input);

  const uint64_t bits = simd_find_any_8x(&input, blank) |
                        simd_find_any_8x(&input, special);
  const uint64_t mask = (-bits & bits) - 1;
  block->length = count_ones(mask);
  block->escape_bits = simd_find_8x(&input, '\\') & mask;
  block->label_bits = simd_find_8x(&input, '.') & mask;
}

zone_nonnull_all()
static zone_inline zone_return_t scan_name(
  zone_parser_t *parser,
  const zone_type_info_t *type,
  const zone_field_info_t *field,
  zone_token_t *token,
  uint8_t octets[255 + ZONE_BLOCK_SIZE],
  size_t *length)
{
  name_block_t block;
  uint8_t *wire = octets + 1, *label = octets;
  const uint8_t *blank, *special;
  const char *text = token->data;

  // based on the token code we set the blank and special tables

  do {
    copy_name_block(&block, blank, special, wire, text);

    if (block.escape_bits) {
    } else {
      text += block.length;
      wire += block.length;
    }

    if (block.label_bits) {
      //
    } else {
      *label += (uint8_t)block.length;
      if (*label > 63)
        SYNTAX_ERROR(parser, "Bad domain name in %s of %s",
                     field->name.data, type->name.data);
    }
  } while (block.length);

  //
  // >> determine the type of table we require...
  //
  // implement
  //
}

zone_nonnull_all()
static zone_inline zone_return_t parse_name(
  zone_parser_t *parser,
  zone_type_info_t *type,
  zone_field_info_t *field,
  zone_token_t *token)
{
  zone_return_t error;

  if ((error = have_string(type, field, token)))
    return error;

  //
  // implement
  //

  return ZONE_NAME;
}

static zone_inline zone_return_t parse_owner(
  zone_parser_t *parser,
  zone_type_info_t *type,
  zone_field_info_t *field,
  zone_token_t *token)
{
  zone_return_t error;

  if ((error = have_string(type, field, token)))
    return error;

  //
  // implement
  //

  return ZONE_NAME;
}

#endif // NAME_H
