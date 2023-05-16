/*
 * zone.h -- (DNS) zone parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef ZONE_H
#define ZONE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <zone/attributes.h>
#include <zone/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup return_code Return codes
 *
 * @{
 */
/** Success */
#define ZONE_SUCCESS (0)
/** Syntax error */
#define ZONE_SYNTAX_ERROR (-(1<<8))
/** Semantic error */
#define ZONE_SEMANTIC_ERROR (-(2<<8))
/** Operation failed due to lack of memory */
#define ZONE_OUT_OF_MEMORY (-(3<<8))
/** Bad parameter value */
#define ZONE_BAD_PARAMETER (-(4<<8))
/** Error reading zone file */
#define ZONE_IO_ERROR (-(5<<8))
/** Control directive or support for record type is not implemented */
#define ZONE_NOT_IMPLEMENTED (-(6<<8))
/** Specified file does not exist */
#define ZONE_NOT_A_FILE (-(7<<8))
/** Access to specified file is not allowed */
#define ZONE_NOT_PERMITTED (-(8<<8))
/** @} */


/** @private */
#define ZONE_BLOCK_SIZE (64)
/** @private */
#define ZONE_WINDOW_SIZE (256 * ZONE_BLOCK_SIZE) // 16KB

 /* (based on experiments, 6 seems decent).*/
#define ZONE_BLOCK_INDEXES (5)

/**
 * @private
 *
 * @brief Number of slots to reserve for storing indexes
 *
 * Tape capacity must be large enough to hold every index from a single
 * worst-case read (e.g. 64 consecutive line feeds). In practice a single
 * block will never contain 64 indexes. To optimize throughput, reserve enough
 * enough space to index the entire window.
 */
#define ZONE_TAPE_SIZE ((256 * ZONE_BLOCK_INDEXES) + ZONE_BLOCK_SIZE)


typedef struct zone_string zone_string_t;
struct zone_string {
  size_t length;
  const char *data;
};

typedef struct zone_name_buffer zone_name_buffer_t;
struct zone_name_buffer {
  size_t length;
  uint8_t octets[ 255 + ZONE_BLOCK_SIZE /* padding */ ];
};

typedef struct zone_rdata_buffer zone_rdata_buffer_t;
struct zone_rdata_buffer {
  size_t length; /**< Length of RDATA stored in buffer */
  uint8_t octets[ 65535 + 4096 /* padding */ ];
};

typedef struct zone_buffers zone_buffers_t;
struct zone_buffers {
  size_t size; /**< Number of name and RDATA buffers available */
  zone_name_buffer_t *owner;
  zone_rdata_buffer_t *rdata;
};

// line feeds are tracked for error reporting. RFC1035 section 5.1 states
// text literals can contain CRLF within the text. BIND9 forbids use of CRLF
// within text literals and it is certainly not common. line feeds are
// collected and flushed per-record
typedef struct zone_index zone_index_t;
struct zone_index {
  const char *data;
  uint32_t lines; // valid if data points to line_feed
};

/** @private */
typedef struct zone_file zone_file_t;
struct zone_file {
  zone_file_t *includer;
  zone_name_buffer_t origin, owner;
  uint16_t last_type;
  uint16_t last_class;
  uint32_t last_ttl, default_ttl;
  size_t line;
  const char *name;
  const char *path;
  FILE *handle;
  bool grouped;
  bool start_of_line;
  enum { ZONE_HAVE_DATA, ZONE_READ_ALL_DATA, ZONE_NO_MORE_DATA } end_of_file;
  struct {
    size_t index, length, size;
    char *data;
  } buffer;
  struct {
    uint32_t lines;
    uint64_t in_comment;
    uint64_t in_quoted;
    uint64_t is_escaped;
    uint64_t follows_contiguous;
    zone_index_t *head, *tail, tape[ZONE_TAPE_SIZE + 2];
  } indexer;
};

typedef struct zone_parser zone_parser_t;
struct zone_parser;

/**
 * @defgroup log_categories Log categories.
 *
 * @note No direct relation between log categories and error codes exists.
 *       Log categories communicate the importance of the log message, error
 *       codes communicate what went wrong to the caller.
 * @{
 */
/** Error condition. */
#define ZONE_ERROR (1u<<1)
/** Warning condition. */
#define ZONE_WARNING (1u<<2)
/** Informational message. */
#define ZONE_INFO (1u<<3)
/** @} */

typedef void(*zone_log_t)(
  zone_parser_t *,
  const char *, // file
  size_t, // line
  const char *, // function
  uint32_t, // category
  const char *, // message
  void *); // user data

/**
 * @brief Write error message to active log handler.
 *
 * @note Direct use is discouraged. Use of #ZONE_LOG instead.
 *
 * @param[in]  parser    Zone parser
 * @param[in]  file      Name of source file
 * @param[in]  line      Line number in source file
 * @param[in]  function  Name of function
 * @param[in]  category  Log category
 * @param[in]  format    Format string compatible with printf
 * @param[in]  ...       Variadic arguments corresponding to #format
 */
ZONE_EXPORT void zone_log(
  zone_parser_t *parser,
  const char *file,
  size_t line,
  const char *function,
  uint32_t category,
  const char *format,
  ...)
zone_nonnull((1,2,4,6))
zone_format_printf(6,7);

/**
 * @brief Write log message to active log handler.
 *
 * The zone parser operates on a per-record base and therefore cannot detect
 * errors that span records. e.g. SOA records being specified more than once.
 * The user may print a message using the active log handler, keeping the
 * error message format consistent.
 *
 * @param[in]  parser    Zone parser
 * @param[in]  category  Log category
 * @param[in]  format    Format string compatible with printf
 * @param[in]  ...       Variadic arguments corresponding to @ref format
 */
#define ZONE_LOG(parser, category, ...) \
  zone_log(parser, __FILE__, __LINE__, __func__, category, __VA_ARGS__)


typedef struct zone_name zone_name_t;
struct zone_name {
  uint8_t length;
  uint8_t *octets;
};

// invoked for each record (host order). header (owner, type, class and ttl)
// fields are passed individually for convenience. rdata fields can be visited
// individually by means of the iterator
typedef int32_t(*zone_add_t)(
  zone_parser_t *,
  const zone_name_t *, // owner (length + octets)
  uint16_t, // type
  uint16_t, // class
  uint32_t, // ttl
  uint16_t, // rdlength
  const uint8_t *, // rdata
  void *); // user data

typedef struct zone_options zone_options_t;
struct zone_options {
  /** Lax mode of operation. */
  /** Authoritative servers may choose to be more lenient when operating as
      as a secondary as data may have been transferred over AXFR/IXFR that
      would have triggered an error otherwise. */
  bool secondary;
  /** Disable $INCLUDE directive. */
  /** Useful in setups where untrusted input may be offered. */
  bool no_includes;
  /** Enable 1h2m3s notation for TTLs. */
  bool friendly_ttls;
  const char *origin;
  uint32_t default_ttl;
  uint16_t default_class;
  struct {
    /** Message categories to write out. */
    /** All categories are printed if no categories are selected and no
        custom callback was specified. */
    uint32_t categories;
    /** Callback used to write out log messages. */
    zone_log_t write;
  } log;
  struct {
    zone_add_t add;
    // FIXME: more callbacks to be added at a later stage to support efficient
    //        (de)serialization of AXFR/IXFR in text representation.
    //zone_delete_t remove;
  } accept;
};


struct zone_parser {
  zone_options_t options;
  void *user_data;
  struct {
    size_t size;
    struct {
      size_t index;
      zone_name_buffer_t *buffers;
    } owner;
    struct {
      size_t index;
      zone_rdata_buffer_t *buffers;
    } rdata;
  } buffers;
  zone_name_buffer_t *owner;
  zone_rdata_buffer_t *rdata;
  zone_file_t *file, first;
};

/**
 * @brief Parse zone from file
 */
ZONE_EXPORT int32_t
zone_parse(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffer,
  const char *path,
  void *user_data)
zone_nonnull((1,2,3,4));

/**
 * @brief Parse zone from string
 */
ZONE_EXPORT int32_t
zone_parse_string(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffer,
  const char *string,
  size_t length,
  void *user_data)
zone_nonnull((1,2,3,4));

// zone_parse_axfr
// zone_parse_ixfr

#ifdef __cplusplus
}
#endif

#endif // ZONE_H
