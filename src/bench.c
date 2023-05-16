/*
 * bench.c -- AVX2 compilation target for benchmark function(s)
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "zone.h"
#include "diagnostic.h"
#include "log.h"
#include "simd.h"
#include "bits.h"
#include "lexer.h"
#include "scanner.h"

diagnostic_push()
clang_diagnostic_ignored(missing-prototypes)

int32_t parse(zone_parser_t *parser, size_t *user_data)
{
  token_t token;
  int32_t result;
  size_t tokens = 0;

  (void)user_data;

  while ((result = lex(parser, &token)) > 0) {
  //  printf("token: (%d), starts with: %c\n", result, *token.data);
    tokens++;
  }

  printf("parsed %zu tokens\n", tokens);

  return result;
}

diagnostic_pop()

int main(int argc, char *argv[])
{
  if (argc != 2)
    return 1;
  zone_parser_t parser;
  zone_options_t options = { 0 };
  zone_name_buffer_t names[1];
  zone_rdata_buffer_t rdatas[1];
  zone_buffers_t buffers = { 1, names, rdatas };

  options.origin = "example.com.";

  int32_t result = zone_parse(&parser, &options, &buffers, argv[1], NULL);

  return result;
}
