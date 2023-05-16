/*
 * zone.c -- zone parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <limits.h>
#if _WIN32
# include <windows.h>
#endif

#include "zone.h"
#include "diagnostic.h"
#include "isadetection.h"

#if _WIN32
#define strcasecmp(s1, s2) _stricmp(s1, s2)
#define strncasecmp(s1, s2, n) _strnicmp(s1, s2, n)
#endif

static const char not_a_file[] = "<string>";

static int32_t check_options(const zone_options_t *options)
{
//  if (!options->accept.add)
//    return ZONE_BAD_PARAMETER;
//  if (!options->origin)
//    return ZONE_BAD_PARAMETER;
//  if (!options->default_ttl || options->default_ttl > INT32_MAX)
//    return ZONE_BAD_PARAMETER;
//  if (!options->default_class)
//    should set default_class to ZONE_IN if 0!

//  switch (options->default_class) {
//    case ZONE_IN:
//    case ZONE_CS:
//    case ZONE_CH:
//    case ZONE_HS:
//      break;
//    default:
//      return ZONE_BAD_PARAMETER;
//  }

  return 0;
}

// support escaped characters here too!
static int parse_origin(const char *origin, uint8_t str[255], size_t *len)
{
  size_t lab = 0, oct = 1;

  assert(origin);

  for (size_t i=0; ; i++) {
    char chr = origin[i];
    if (oct >= 255)
      return -1;

    if (chr == '.' || chr == '\0') {
      if (oct - 1 == lab && lab > 0 && chr != '\0')
        return -1;
      else if ((oct - lab) - 1 > 63)
        return -1;
      str[lab] = (uint8_t)((oct - lab) - 1);
      if (chr != '.')
        break;
      lab = oct++;
      str[lab] = 0;
    } else {
      str[oct++] = chr & 0xff;
    }
  }

  if (str[lab] != 0)
    return -1;

  *len = oct;
  return 0;
}

//#include "config.h"
#include "isadetection.h"

zone_nonnull_all()
static int32_t open_file(
  zone_parser_t *parser, zone_file_t *file, const zone_string_t *path)
{
  if (!(file->name = strndup(path->data, path->length)))
    return ZONE_OUT_OF_MEMORY;

#if _WIN32
  char buf[1];
  size_t length, size = GetFullPathName(file->name, sizeof(buf), buf, NULL);
  if (!size)
    return ZONE_IO_ERROR;
  if (!(file->path = malloc(parser, size)))
    return ZONE_OUT_OF_MEMORY;
  if (!(length = GetFullPathName(file->name, size, file->path, NULL)))
    return ZONE_IO_ERROR;
  if (length != size - 1)
    return ZONE_IO_ERROR;
#else
  char buf[PATH_MAX];
  if (!realpath(file->name, buf))
    return ZONE_IO_ERROR;
  if (!(file->path = strdup(buf)))
    return ZONE_OUT_OF_MEMORY;
#endif

  if (!(file->handle = fopen(file->path, "rb")))
    switch (errno) {
      case ENOMEM:
        return ZONE_OUT_OF_MEMORY;
      default:
        return ZONE_IO_ERROR;
    }

  if (!(file->buffer.data = malloc(ZONE_WINDOW_SIZE + 1)))
    return ZONE_OUT_OF_MEMORY;

  file->buffer.data[0] = '\0';
  file->buffer.size = ZONE_WINDOW_SIZE;
  file->buffer.length = 0;
  file->buffer.index = 0;
  file->start_of_line = true;
  file->end_of_file = ZONE_HAVE_DATA;
  file->indexer.tape[0].data = file->buffer.data;
  file->indexer.tape[1].data = NULL;
  file->indexer.head = file->indexer.tape;
  file->indexer.tail = file->indexer.tape;
  return 0;
}

static void set_defaults(zone_parser_t *parser)
{
  if (!parser->options.log.write && !parser->options.log.categories)
    parser->options.log.categories = (uint32_t)-1;
  parser->owner = &parser->file->owner;
  parser->rdata = &parser->buffers.rdata.buffers[0];
}

diagnostic_push()
clang_diagnostic_ignored(missing-prototypes)

zone_nonnull_all()
void zone_close_file(
  zone_parser_t *parser, zone_file_t *file)
{
  assert((file->name == not_a_file) == !file->handle);
  assert((file->path == not_a_file) == !file->handle);

  if (!file->handle)
    return;

  if (file->buffer.data)
    free(file->buffer.data);
  file->buffer.data = NULL;
  if (file->name)
    free((char *)file->name);
  file->name = NULL;
  if (file->path)
    free((char *)file->path);
  file->path = NULL;
  (void)fclose(file->handle);
  file->handle = NULL;
  if (file != &parser->first)
    free(file);
}

zone_nonnull_all()
int32_t zone_open_file(
  zone_parser_t *parser, const zone_string_t *path, zone_file_t **fileptr)
{
  int32_t result;
  zone_file_t *file;

  if (!(file = malloc(sizeof(*file))))
    return ZONE_OUT_OF_MEMORY;
  memset(file, 0, sizeof(*file) - sizeof(file->indexer.tape));
  if ((result = open_file(parser, file, path)) < 0)
    goto err_open;

  *fileptr = file;
  return 0;
err_open:
  zone_close_file(parser, file);
  return result;
}

void zone_close(zone_parser_t *parser)
{
  if (!parser)
    return;

  for (zone_file_t *file = parser->file, *includer; file; file = includer) {
    includer = file->includer;
    if (file->handle)
      zone_close_file(parser, file);
  }
}

int32_t zone_open(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *path,
  void *user_data)
{
  int32_t result;
  zone_file_t *file;

  if ((result = check_options(options)) < 0)
    return result;

  memset(parser, 0, sizeof(*parser));
  parser->options = *options;
  parser->user_data = user_data;
  file = parser->file = &parser->first;
  if ((result = open_file(parser, file, &(zone_string_t){ strlen(path), path })) < 0)
    goto error;
  if (parse_origin(options->origin, file->origin.octets, &file->origin.length) < 0) {
    result = ZONE_BAD_PARAMETER;
    goto error;
  }
  parser->buffers.size = buffers->size;
  parser->buffers.owner.index = 0;
  parser->buffers.owner.buffers = buffers->owner;
  parser->buffers.rdata.index = 0;
  parser->buffers.rdata.buffers = buffers->rdata;
  file->owner = file->origin;
  file->last_type = 0;
  file->last_class = options->default_class;
  file->last_ttl = options->default_ttl;
  file->line = 1;

  set_defaults(parser);
  return 0;
error:
  zone_close(parser);
  return result;
}

diagnostic_pop()

extern int32_t parse(zone_parser_t *parser, void *user_data);

int32_t zone_parse(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *path,
  void *user_data)
{
  int32_t result;

  if ((result = zone_open(parser, options, buffers, path, user_data)) < 0)
    return result;
  result = parse(parser, user_data);
  zone_close(parser);
  return result;
}

int32_t zone_parse_string(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *string,
  size_t length,
  void *user_data)
{
  int32_t result;
  zone_file_t *file;

  if ((result = check_options(options)) < 0)
    return result;

  memset(parser, 0, sizeof(*parser));
  parser->options = *options;
  parser->user_data = user_data;
  file = parser->file = &parser->first;
  if ((result = parse_origin(options->origin, file->origin.octets, &file->origin.length)) < 0)
    return result;

  file->name = not_a_file;
  file->path = not_a_file;
  file->handle = NULL;
  file->buffer.index = 0;
  file->buffer.length = length;
  file->buffer.size = length;
  file->buffer.data = (char *)string;
  file->start_of_line = true;
  file->end_of_file = ZONE_READ_ALL_DATA;
  file->indexer.tape[0].data = "\0";
  file->indexer.tape[1].data = NULL;
  file->indexer.head = file->indexer.tape;
  file->indexer.tail = file->indexer.tape;

  parser->buffers.size = buffers->size;
  parser->buffers.owner.index = 0;
  parser->buffers.owner.buffers = buffers->owner;
  parser->buffers.rdata.index = 0;
  parser->buffers.rdata.buffers = buffers->rdata;
  file->owner = file->origin;
  file->last_type = 0;
  file->last_class = options->default_class;
  file->last_ttl = options->default_ttl;
  file->line = 1;

  set_defaults(parser);
  result = parse(parser, user_data);
  zone_close(parser);
  return result;
}
