/*
 * Headers
*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


/*
 * Initialisation
*/

struct futhark_context_config ;
struct futhark_context_config *futhark_context_config_new();
void futhark_context_config_free(struct futhark_context_config *cfg);
void futhark_context_config_set_debugging(struct futhark_context_config *cfg,
                                          int flag);
void futhark_context_config_set_logging(struct futhark_context_config *cfg,
                                        int flag);
struct futhark_context ;
struct futhark_context *futhark_context_new(struct futhark_context_config *cfg);
void futhark_context_free(struct futhark_context *ctx);
int futhark_context_sync(struct futhark_context *ctx);
char *futhark_context_get_error(struct futhark_context *ctx);

/*
 * Arrays
*/


/*
 * Opaque values
*/


/*
 * Entry points
*/

int futhark_entry_main(struct futhark_context *ctx, bool *out0);

/*
 * Miscellaneous
*/

void futhark_debugging_report(struct futhark_context *ctx);
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#undef NDEBUG
#include <assert.h>
/* Crash and burn. */

#include <stdarg.h>

static const char *fut_progname;

static void panic(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
        fprintf(stderr, "%s: ", fut_progname);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
        exit(eval);
}

/* For generating arbitrary-sized error messages.  It is the callers
   responsibility to free the buffer at some point. */
static char* msgprintf(const char *s, ...) {
  va_list vl;
  va_start(vl, s);
  size_t needed = 1 + vsnprintf(NULL, 0, s, vl);
  char *buffer = malloc(needed);
  va_start(vl, s); /* Must re-init. */
  vsnprintf(buffer, needed, s, vl);
  return buffer;
}

/* Some simple utilities for wall-clock timing.

   The function get_wall_time() returns the wall time in microseconds
   (with an unspecified offset).
*/

#ifdef _WIN32

#include <windows.h>

static int64_t get_wall_time(void) {
  LARGE_INTEGER time,freq;
  assert(QueryPerformanceFrequency(&freq));
  assert(QueryPerformanceCounter(&time));
  return ((double)time.QuadPart / freq.QuadPart) * 1000000;
}

#else
/* Assuming POSIX */

#include <time.h>
#include <sys/time.h>

static int64_t get_wall_time(void) {
  struct timeval time;
  assert(gettimeofday(&time,NULL) == 0);
  return time.tv_sec * 1000000 + time.tv_usec;
}

#endif

#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
//// Text I/O

typedef int (*writer)(FILE*, void*);
typedef int (*bin_reader)(void*);
typedef int (*str_reader)(const char *, void*);

struct array_reader {
  char* elems;
  int64_t n_elems_space;
  int64_t elem_size;
  int64_t n_elems_used;
  int64_t *shape;
  str_reader elem_reader;
};

static void skipspaces() {
  int c;
  do {
    c = getchar();
  } while (isspace(c));

  if (c != EOF) {
    ungetc(c, stdin);
  }
}

static int constituent(char c) {
  return isalnum(c) || c == '.' || c == '-' || c == '+' || c == '_';
}

// Produces an empty token only on EOF.
static void next_token(char *buf, int bufsize) {
 start:
  skipspaces();

  int i = 0;
  while (i < bufsize) {
    int c = getchar();
    buf[i] = c;

    if (c == EOF) {
      buf[i] = 0;
      return;
    } else if (c == '-' && i == 1 && buf[0] == '-') {
      // Line comment, so skip to end of line and start over.
      for (; c != '\n' && c != EOF; c = getchar());
      goto start;
    } else if (!constituent(c)) {
      if (i == 0) {
        // We permit single-character tokens that are not
        // constituents; this lets things like ']' and ',' be
        // tokens.
        buf[i+1] = 0;
        return;
      } else {
        ungetc(c, stdin);
        buf[i] = 0;
        return;
      }
    }

    i++;
  }

  buf[bufsize-1] = 0;
}

static int next_token_is(char *buf, int bufsize, const char* expected) {
  next_token(buf, bufsize);
  return strcmp(buf, expected) == 0;
}

static void remove_underscores(char *buf) {
  char *w = buf;

  for (char *r = buf; *r; r++) {
    if (*r != '_') {
      *w++ = *r;
    }
  }

  *w++ = 0;
}

static int read_str_elem(char *buf, struct array_reader *reader) {
  int ret;
  if (reader->n_elems_used == reader->n_elems_space) {
    reader->n_elems_space *= 2;
    reader->elems = (char*) realloc(reader->elems,
                                    reader->n_elems_space * reader->elem_size);
  }

  ret = reader->elem_reader(buf, reader->elems + reader->n_elems_used * reader->elem_size);

  if (ret == 0) {
    reader->n_elems_used++;
  }

  return ret;
}

static int read_str_array_elems(char *buf, int bufsize,
                                struct array_reader *reader, int dims) {
  int ret;
  int first = 1;
  char *knows_dimsize = (char*) calloc(dims,sizeof(char));
  int cur_dim = dims-1;
  int64_t *elems_read_in_dim = (int64_t*) calloc(dims,sizeof(int64_t));

  while (1) {
    next_token(buf, bufsize);

    if (strcmp(buf, "]") == 0) {
      if (knows_dimsize[cur_dim]) {
        if (reader->shape[cur_dim] != elems_read_in_dim[cur_dim]) {
          ret = 1;
          break;
        }
      } else {
        knows_dimsize[cur_dim] = 1;
        reader->shape[cur_dim] = elems_read_in_dim[cur_dim];
      }
      if (cur_dim == 0) {
        ret = 0;
        break;
      } else {
        cur_dim--;
        elems_read_in_dim[cur_dim]++;
      }
    } else if (strcmp(buf, ",") == 0) {
      next_token(buf, bufsize);
      if (strcmp(buf, "[") == 0) {
        if (cur_dim == dims - 1) {
          ret = 1;
          break;
        }
        first = 1;
        cur_dim++;
        elems_read_in_dim[cur_dim] = 0;
      } else if (cur_dim == dims - 1) {
        ret = read_str_elem(buf, reader);
        if (ret != 0) {
          break;
        }
        elems_read_in_dim[cur_dim]++;
      } else {
        ret = 1;
        break;
      }
    } else if (strlen(buf) == 0) {
      // EOF
      ret = 1;
      break;
    } else if (first) {
      if (strcmp(buf, "[") == 0) {
        if (cur_dim == dims - 1) {
          ret = 1;
          break;
        }
        cur_dim++;
        elems_read_in_dim[cur_dim] = 0;
      } else {
        ret = read_str_elem(buf, reader);
        if (ret != 0) {
          break;
        }
        elems_read_in_dim[cur_dim]++;
        first = 0;
      }
    } else {
      ret = 1;
      break;
    }
  }

  free(knows_dimsize);
  free(elems_read_in_dim);
  return ret;
}

static int read_str_empty_array(char *buf, int bufsize,
                                const char *type_name, int64_t *shape, int64_t dims) {
  if (strlen(buf) == 0) {
    // EOF
    return 1;
  }

  if (strcmp(buf, "empty") != 0) {
    return 1;
  }

  if (!next_token_is(buf, bufsize, "(")) {
    return 1;
  }

  for (int i = 0; i < dims-1; i++) {
    if (!next_token_is(buf, bufsize, "[")) {
      return 1;
    }

    if (!next_token_is(buf, bufsize, "]")) {
      return 1;
    }
  }

  if (!next_token_is(buf, bufsize, type_name)) {
    return 1;
  }


  if (!next_token_is(buf, bufsize, ")")) {
    return 1;
  }

  for (int i = 0; i < dims; i++) {
    shape[i] = 0;
  }

  return 0;
}

static int read_str_array(int64_t elem_size, str_reader elem_reader,
                          const char *type_name,
                          void **data, int64_t *shape, int64_t dims) {
  int ret;
  struct array_reader reader;
  char buf[100];

  int dims_seen;
  for (dims_seen = 0; dims_seen < dims; dims_seen++) {
    if (!next_token_is(buf, sizeof(buf), "[")) {
      break;
    }
  }

  if (dims_seen == 0) {
    return read_str_empty_array(buf, sizeof(buf), type_name, shape, dims);
  }

  if (dims_seen != dims) {
    return 1;
  }

  reader.shape = shape;
  reader.n_elems_used = 0;
  reader.elem_size = elem_size;
  reader.n_elems_space = 16;
  reader.elems = (char*) realloc(*data, elem_size*reader.n_elems_space);
  reader.elem_reader = elem_reader;

  ret = read_str_array_elems(buf, sizeof(buf), &reader, dims);

  *data = reader.elems;

  return ret;
}

#define READ_STR(MACRO, PTR, SUFFIX)                                   \
  remove_underscores(buf);                                              \
  int j;                                                                \
  if (sscanf(buf, "%"MACRO"%n", (PTR*)dest, &j) == 1) {                 \
    return !(strcmp(buf+j, "") == 0 || strcmp(buf+j, SUFFIX) == 0);     \
  } else {                                                              \
    return 1;                                                           \
  }

static int read_str_i8(char *buf, void* dest) {
  /* Some platforms (WINDOWS) does not support scanf %hhd or its
     cousin, %SCNi8.  Read into int first to avoid corrupting
     memory.

     https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63417  */
  remove_underscores(buf);
  int j, x;
  if (sscanf(buf, "%i%n", &x, &j) == 1) {
    *(int8_t*)dest = x;
    return !(strcmp(buf+j, "") == 0 || strcmp(buf+j, "i8") == 0);
  } else {
    return 1;
  }
}

static int read_str_u8(char *buf, void* dest) {
  /* Some platforms (WINDOWS) does not support scanf %hhd or its
     cousin, %SCNu8.  Read into int first to avoid corrupting
     memory.

     https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63417  */
  remove_underscores(buf);
  int j, x;
  if (sscanf(buf, "%i%n", &x, &j) == 1) {
    *(uint8_t*)dest = x;
    return !(strcmp(buf+j, "") == 0 || strcmp(buf+j, "u8") == 0);
  } else {
    return 1;
  }
}

static int read_str_i16(char *buf, void* dest) {
  READ_STR(SCNi16, int16_t, "i16");
}

static int read_str_u16(char *buf, void* dest) {
  READ_STR(SCNi16, int16_t, "u16");
}

static int read_str_i32(char *buf, void* dest) {
  READ_STR(SCNi32, int32_t, "i32");
}

static int read_str_u32(char *buf, void* dest) {
  READ_STR(SCNi32, int32_t, "u32");
}

static int read_str_i64(char *buf, void* dest) {
  READ_STR(SCNi64, int64_t, "i64");
}

static int read_str_u64(char *buf, void* dest) {
  // FIXME: This is not correct, as SCNu64 only permits decimal
  // literals.  However, SCNi64 does not handle very large numbers
  // correctly (it's really for signed numbers, so that's fair).
  READ_STR(SCNu64, uint64_t, "u64");
}

static int read_str_f32(char *buf, void* dest) {
  remove_underscores(buf);
  if (strcmp(buf, "f32.nan") == 0) {
    *(float*)dest = NAN;
    return 0;
  } else if (strcmp(buf, "f32.inf") == 0) {
    *(float*)dest = INFINITY;
    return 0;
  } else if (strcmp(buf, "-f32.inf") == 0) {
    *(float*)dest = -INFINITY;
    return 0;
  } else {
    READ_STR("f", float, "f32");
  }
}

static int read_str_f64(char *buf, void* dest) {
  remove_underscores(buf);
  if (strcmp(buf, "f64.nan") == 0) {
    *(double*)dest = NAN;
    return 0;
  } else if (strcmp(buf, "f64.inf") == 0) {
    *(double*)dest = INFINITY;
    return 0;
  } else if (strcmp(buf, "-f64.inf") == 0) {
    *(double*)dest = -INFINITY;
    return 0;
  } else {
    READ_STR("lf", double, "f64");
  }
}

static int read_str_bool(char *buf, void* dest) {
  if (strcmp(buf, "true") == 0) {
    *(char*)dest = 1;
    return 0;
  } else if (strcmp(buf, "false") == 0) {
    *(char*)dest = 0;
    return 0;
  } else {
    return 1;
  }
}

static int write_str_i8(FILE *out, int8_t *src) {
  return fprintf(out, "%hhdi8", *src);
}

static int write_str_u8(FILE *out, uint8_t *src) {
  return fprintf(out, "%hhuu8", *src);
}

static int write_str_i16(FILE *out, int16_t *src) {
  return fprintf(out, "%hdi16", *src);
}

static int write_str_u16(FILE *out, uint16_t *src) {
  return fprintf(out, "%huu16", *src);
}

static int write_str_i32(FILE *out, int32_t *src) {
  return fprintf(out, "%di32", *src);
}

static int write_str_u32(FILE *out, uint32_t *src) {
  return fprintf(out, "%uu32", *src);
}

static int write_str_i64(FILE *out, int64_t *src) {
  return fprintf(out, "%"PRIi64"i64", *src);
}

static int write_str_u64(FILE *out, uint64_t *src) {
  return fprintf(out, "%"PRIu64"u64", *src);
}

static int write_str_f32(FILE *out, float *src) {
  float x = *src;
  if (isnan(x)) {
    return fprintf(out, "f32.nan");
  } else if (isinf(x) && x >= 0) {
    return fprintf(out, "f32.inf");
  } else if (isinf(x)) {
    return fprintf(out, "-f32.inf");
  } else {
    return fprintf(out, "%.6ff32", x);
  }
}

static int write_str_f64(FILE *out, double *src) {
  double x = *src;
  if (isnan(x)) {
    return fprintf(out, "f64.nan");
  } else if (isinf(x) && x >= 0) {
    return fprintf(out, "f64.inf");
  } else if (isinf(x)) {
    return fprintf(out, "-f64.inf");
  } else {
    return fprintf(out, "%.6ff64", *src);
  }
}

static int write_str_bool(FILE *out, void *src) {
  return fprintf(out, *(char*)src ? "true" : "false");
}

//// Binary I/O

#define BINARY_FORMAT_VERSION 2
#define IS_BIG_ENDIAN (!*(unsigned char *)&(uint16_t){1})

// Reading little-endian byte sequences.  On big-endian hosts, we flip
// the resulting bytes.

static int read_byte(void* dest) {
  int num_elems_read = fread(dest, 1, 1, stdin);
  return num_elems_read == 1 ? 0 : 1;
}

static int read_le_2byte(void* dest) {
  uint16_t x;
  int num_elems_read = fread(&x, 2, 1, stdin);
  if (IS_BIG_ENDIAN) {
    x = (x>>8) | (x<<8);
  }
  *(uint16_t*)dest = x;
  return num_elems_read == 1 ? 0 : 1;
}

static int read_le_4byte(void* dest) {
  uint32_t x;
  int num_elems_read = fread(&x, 4, 1, stdin);
  if (IS_BIG_ENDIAN) {
    x =
      ((x>>24)&0xFF) |
      ((x>>8) &0xFF00) |
      ((x<<8) &0xFF0000) |
      ((x<<24)&0xFF000000);
  }
  *(uint32_t*)dest = x;
  return num_elems_read == 1 ? 0 : 1;
}

static int read_le_8byte(void* dest) {
  uint64_t x;
  int num_elems_read = fread(&x, 8, 1, stdin);
  if (IS_BIG_ENDIAN) {
    x =
      ((x>>56)&0xFFull) |
      ((x>>40)&0xFF00ull) |
      ((x>>24)&0xFF0000ull) |
      ((x>>8) &0xFF000000ull) |
      ((x<<8) &0xFF00000000ull) |
      ((x<<24)&0xFF0000000000ull) |
      ((x<<40)&0xFF000000000000ull) |
      ((x<<56)&0xFF00000000000000ull);
  }
  *(uint64_t*)dest = x;
  return num_elems_read == 1 ? 0 : 1;
}

static int write_byte(void* dest) {
  int num_elems_written = fwrite(dest, 1, 1, stdin);
  return num_elems_written == 1 ? 0 : 1;
}

static int write_le_2byte(void* dest) {
  uint16_t x = *(uint16_t*)dest;
  if (IS_BIG_ENDIAN) {
    x = (x>>8) | (x<<8);
  }
  int num_elems_written = fwrite(&x, 2, 1, stdin);
  return num_elems_written == 1 ? 0 : 1;
}

static int write_le_4byte(void* dest) {
  uint32_t x = *(uint32_t*)dest;
  if (IS_BIG_ENDIAN) {
    x =
      ((x>>24)&0xFF) |
      ((x>>8) &0xFF00) |
      ((x<<8) &0xFF0000) |
      ((x<<24)&0xFF000000);
  }
  int num_elems_written = fwrite(&x, 4, 1, stdin);
  return num_elems_written == 1 ? 0 : 1;
}

static int write_le_8byte(void* dest) {
  uint64_t x = *(uint64_t*)dest;
  if (IS_BIG_ENDIAN) {
    x =
      ((x>>56)&0xFFull) |
      ((x>>40)&0xFF00ull) |
      ((x>>24)&0xFF0000ull) |
      ((x>>8) &0xFF000000ull) |
      ((x<<8) &0xFF00000000ull) |
      ((x<<24)&0xFF0000000000ull) |
      ((x<<40)&0xFF000000000000ull) |
      ((x<<56)&0xFF00000000000000ull);
  }
  int num_elems_written = fwrite(&x, 8, 1, stdin);
  return num_elems_written == 1 ? 0 : 1;
}

//// Types

struct primtype_info_t {
  const char binname[4]; // Used for parsing binary data.
  const char* type_name; // Same name as in Futhark.
  const int size; // in bytes
  const writer write_str; // Write in text format.
  const str_reader read_str; // Read in text format.
  const writer write_bin; // Write in binary format.
  const bin_reader read_bin; // Read in binary format.
};

static const struct primtype_info_t i8_info =
  {.binname = "  i8", .type_name = "i8",   .size = 1,
   .write_str = (writer)write_str_i8, .read_str = (str_reader)read_str_i8,
   .write_bin = (writer)write_byte, .read_bin = (bin_reader)read_byte};
static const struct primtype_info_t i16_info =
  {.binname = " i16", .type_name = "i16",  .size = 2,
   .write_str = (writer)write_str_i16, .read_str = (str_reader)read_str_i16,
   .write_bin = (writer)write_le_2byte, .read_bin = (bin_reader)read_le_2byte};
static const struct primtype_info_t i32_info =
  {.binname = " i32", .type_name = "i32",  .size = 4,
   .write_str = (writer)write_str_i32, .read_str = (str_reader)read_str_i32,
   .write_bin = (writer)write_le_4byte, .read_bin = (bin_reader)read_le_4byte};
static const struct primtype_info_t i64_info =
  {.binname = " i64", .type_name = "i64",  .size = 8,
   .write_str = (writer)write_str_i64, .read_str = (str_reader)read_str_i64,
   .write_bin = (writer)write_le_8byte, .read_bin = (bin_reader)read_le_8byte};
static const struct primtype_info_t u8_info =
  {.binname = "  u8", .type_name = "u8",   .size = 1,
   .write_str = (writer)write_str_u8, .read_str = (str_reader)read_str_u8,
   .write_bin = (writer)write_byte, .read_bin = (bin_reader)read_byte};
static const struct primtype_info_t u16_info =
  {.binname = " u16", .type_name = "u16",  .size = 2,
   .write_str = (writer)write_str_u16, .read_str = (str_reader)read_str_u16,
   .write_bin = (writer)write_le_2byte, .read_bin = (bin_reader)read_le_2byte};
static const struct primtype_info_t u32_info =
  {.binname = " u32", .type_name = "u32",  .size = 4,
   .write_str = (writer)write_str_u32, .read_str = (str_reader)read_str_u32,
   .write_bin = (writer)write_le_4byte, .read_bin = (bin_reader)read_le_4byte};
static const struct primtype_info_t u64_info =
  {.binname = " u64", .type_name = "u64",  .size = 8,
   .write_str = (writer)write_str_u64, .read_str = (str_reader)read_str_u64,
   .write_bin = (writer)write_le_8byte, .read_bin = (bin_reader)read_le_8byte};
static const struct primtype_info_t f32_info =
  {.binname = " f32", .type_name = "f32",  .size = 4,
   .write_str = (writer)write_str_f32, .read_str = (str_reader)read_str_f32,
   .write_bin = (writer)write_le_4byte, .read_bin = (bin_reader)read_le_4byte};
static const struct primtype_info_t f64_info =
  {.binname = " f64", .type_name = "f64",  .size = 8,
   .write_str = (writer)write_str_f64, .read_str = (str_reader)read_str_f64,
   .write_bin = (writer)write_le_8byte, .read_bin = (bin_reader)read_le_8byte};
static const struct primtype_info_t bool_info =
  {.binname = "bool", .type_name = "bool", .size = 1,
   .write_str = (writer)write_str_bool, .read_str = (str_reader)read_str_bool,
   .write_bin = (writer)write_byte, .read_bin = (bin_reader)read_byte};

static const struct primtype_info_t* primtypes[] = {
  &i8_info, &i16_info, &i32_info, &i64_info,
  &u8_info, &u16_info, &u32_info, &u64_info,
  &f32_info, &f64_info,
  &bool_info,
  NULL // NULL-terminated
};

// General value interface.  All endian business taken care of at
// lower layers.

static int read_is_binary() {
  skipspaces();
  int c = getchar();
  if (c == 'b') {
    int8_t bin_version;
    int ret = read_byte(&bin_version);

    if (ret != 0) { panic(1, "binary-input: could not read version.\n"); }

    if (bin_version != BINARY_FORMAT_VERSION) {
      panic(1, "binary-input: File uses version %i, but I only understand version %i.\n",
            bin_version, BINARY_FORMAT_VERSION);
    }

    return 1;
  }
  ungetc(c, stdin);
  return 0;
}

static const struct primtype_info_t* read_bin_read_type_enum() {
  char read_binname[4];

  int num_matched = scanf("%4c", read_binname);
  if (num_matched != 1) { panic(1, "binary-input: Couldn't read element type.\n"); }

  const struct primtype_info_t **type = primtypes;

  for (; *type != NULL; type++) {
    // I compare the 4 characters manually instead of using strncmp because
    // this allows any value to be used, also NULL bytes
    if (memcmp(read_binname, (*type)->binname, 4) == 0) {
      return *type;
    }
  }
  panic(1, "binary-input: Did not recognize the type '%s'.\n", read_binname);
  return NULL;
}

static void read_bin_ensure_scalar(const struct primtype_info_t *expected_type) {
  int8_t bin_dims;
  int ret = read_byte(&bin_dims);
  if (ret != 0) { panic(1, "binary-input: Couldn't get dims.\n"); }

  if (bin_dims != 0) {
    panic(1, "binary-input: Expected scalar (0 dimensions), but got array with %i dimensions.\n",
          bin_dims);
  }

  const struct primtype_info_t *bin_type = read_bin_read_type_enum();
  if (bin_type != expected_type) {
    panic(1, "binary-input: Expected scalar of type %s but got scalar of type %s.\n",
          expected_type->type_name,
          bin_type->type_name);
  }
}

//// High-level interface

static int read_bin_array(const struct primtype_info_t *expected_type, void **data, int64_t *shape, int64_t dims) {
  int ret;

  int8_t bin_dims;
  ret = read_byte(&bin_dims);
  if (ret != 0) { panic(1, "binary-input: Couldn't get dims.\n"); }

  if (bin_dims != dims) {
    panic(1, "binary-input: Expected %i dimensions, but got array with %i dimensions.\n",
          dims, bin_dims);
  }

  const struct primtype_info_t *bin_primtype = read_bin_read_type_enum();
  if (expected_type != bin_primtype) {
    panic(1, "binary-input: Expected %iD-array with element type '%s' but got %iD-array with element type '%s'.\n",
          dims, expected_type->type_name, dims, bin_primtype->type_name);
  }

  uint64_t elem_count = 1;
  for (int i=0; i<dims; i++) {
    uint64_t bin_shape;
    ret = read_le_8byte(&bin_shape);
    if (ret != 0) { panic(1, "binary-input: Couldn't read size for dimension %i of array.\n", i); }
    elem_count *= bin_shape;
    shape[i] = (int64_t) bin_shape;
  }

  size_t elem_size = expected_type->size;
  void* tmp = realloc(*data, elem_count * elem_size);
  if (tmp == NULL) {
    panic(1, "binary-input: Failed to allocate array of size %i.\n",
          elem_count * elem_size);
  }
  *data = tmp;

  size_t num_elems_read = fread(*data, elem_size, elem_count, stdin);
  if (num_elems_read != elem_count) {
    panic(1, "binary-input: tried to read %i elements of an array, but only got %i elements.\n",
          elem_count, num_elems_read);
  }

  // If we're on big endian platform we must change all multibyte elements
  // from using little endian to big endian
  if (IS_BIG_ENDIAN && elem_size != 1) {
    char* elems = (char*) *data;
    for (uint64_t i=0; i<elem_count; i++) {
      char* elem = elems+(i*elem_size);
      for (unsigned int j=0; j<elem_size/2; j++) {
        char head = elem[j];
        int tail_index = elem_size-1-j;
        elem[j] = elem[tail_index];
        elem[tail_index] = head;
      }
    }
  }

  return 0;
}

static int read_array(const struct primtype_info_t *expected_type, void **data, int64_t *shape, int64_t dims) {
  if (!read_is_binary()) {
    return read_str_array(expected_type->size, (str_reader)expected_type->read_str, expected_type->type_name, data, shape, dims);
  } else {
    return read_bin_array(expected_type, data, shape, dims);
  }
}

static int write_str_array(FILE *out, const struct primtype_info_t *elem_type, unsigned char *data, int64_t *shape, int8_t rank) {
  if (rank==0) {
    elem_type->write_str(out, (void*)data);
  } else {
    int64_t len = shape[0];
    int64_t slice_size = 1;

    int64_t elem_size = elem_type->size;
    for (int64_t i = 1; i < rank; i++) {
      slice_size *= shape[i];
    }

    if (len*slice_size == 0) {
      printf("empty(");
      for (int64_t i = 1; i < rank; i++) {
        printf("[]");
      }
      printf("%s", elem_type->type_name);
      printf(")");
    } else if (rank==1) {
      putchar('[');
      for (int64_t i = 0; i < len; i++) {
        elem_type->write_str(out, (void*) (data + i * elem_size));
        if (i != len-1) {
          printf(", ");
        }
      }
      putchar(']');
    } else {
      putchar('[');
      for (int64_t i = 0; i < len; i++) {
        write_str_array(out, elem_type, data + i * slice_size * elem_size, shape+1, rank-1);
        if (i != len-1) {
          printf(", ");
        }
      }
      putchar(']');
    }
  }
  return 0;
}

static int write_bin_array(FILE *out, const struct primtype_info_t *elem_type, unsigned char *data, int64_t *shape, int8_t rank) {
  int64_t num_elems = 1;
  for (int64_t i = 0; i < rank; i++) {
    num_elems *= shape[i];
  }

  fputc('b', out);
  fputc((char)BINARY_FORMAT_VERSION, out);
  fwrite(&rank, sizeof(int8_t), 1, out);
  fputs(elem_type->binname, out);
  fwrite(shape, sizeof(int64_t), rank, out);

  if (IS_BIG_ENDIAN) {
    for (int64_t i = 0; i < num_elems; i++) {
      unsigned char *elem = data+i*elem_type->size;
      for (int64_t j = 0; j < elem_type->size; j++) {
        fwrite(&elem[elem_type->size-j], 1, 1, out);
      }
    }
  } else {
    fwrite(data, elem_type->size, num_elems, out);
  }

  return 0;
}

static int write_array(FILE *out, int write_binary,
                       const struct primtype_info_t *elem_type, void *data, int64_t *shape, int8_t rank) {
  if (write_binary) {
    return write_bin_array(out, elem_type, data, shape, rank);
  } else {
    return write_str_array(out, elem_type, data, shape, rank);
  }
}

static int read_scalar(const struct primtype_info_t *expected_type, void *dest) {
  if (!read_is_binary()) {
    char buf[100];
    next_token(buf, sizeof(buf));
    return expected_type->read_str(buf, dest);
  } else {
    read_bin_ensure_scalar(expected_type);
    return expected_type->read_bin(dest);
  }
}

static int write_scalar(FILE *out, int write_binary, const struct primtype_info_t *type, void *src) {
  if (write_binary) {
    return write_bin_array(out, type, src, NULL, 0);
  } else {
    return type->write_str(out, src);
  }
}

static int binary_output = 0;
static FILE *runtime_file;
static int perform_warmup = 0;
static int num_runs = 1;
static const char *entry_point = "main";
int parse_options(struct futhark_context_config *cfg, int argc,
                  char *const argv[])
{
    int ch;
    static struct option long_options[] = {{"write-runtime-to",
                                            required_argument, NULL, 1},
                                           {"runs", required_argument, NULL, 2},
                                           {"debugging", no_argument, NULL, 3},
                                           {"log", no_argument, NULL, 4},
                                           {"entry-point", required_argument,
                                            NULL, 5}, {"binary-output",
                                                       no_argument, NULL, 6},
                                           {0, 0, 0, 0}};
    
    while ((ch = getopt_long(argc, argv, ":t:r:DLe:b", long_options, NULL)) !=
           -1) {
        if (ch == 1 || ch == 't') {
            runtime_file = fopen(optarg, "w");
            if (runtime_file == NULL)
                panic(1, "Cannot open %s: %s\n", optarg, strerror(errno));
        }
        if (ch == 2 || ch == 'r') {
            num_runs = atoi(optarg);
            perform_warmup = 1;
            if (num_runs <= 0)
                panic(1, "Need a positive number of runs, not %s\n", optarg);
        }
        if (ch == 3 || ch == 'D')
            futhark_context_config_set_debugging(cfg, 1);
        if (ch == 4 || ch == 'L')
            futhark_context_config_set_logging(cfg, 1);
        if (ch == 5 || ch == 'e')
            entry_point = optarg;
        if (ch == 6 || ch == 'b')
            binary_output = 1;
        if (ch == ':')
            panic(-1, "Missing argument for option %s\n", argv[optind - 1]);
        if (ch == '?')
            panic(-1, "Unknown option %s\n", argv[optind - 1]);
    }
    return optind;
}
static void futrts_cli_entry_main(struct futhark_context *ctx)
{
    int64_t t_start, t_end;
    int time_runs;
    bool result_29317;
    
    if (perform_warmup) {
        time_runs = 0;
        
        int r;
        
        assert(futhark_context_sync(ctx) == 0);
        t_start = get_wall_time();
        r = futhark_entry_main(ctx, &result_29317);
        if (r != 0)
            panic(1, "%s", futhark_context_get_error(ctx));
        assert(futhark_context_sync(ctx) == 0);
        t_end = get_wall_time();
        
        long elapsed_usec = t_end - t_start;
        
        if (time_runs && runtime_file != NULL)
            fprintf(runtime_file, "%lld\n", (long long) elapsed_usec);
        ;
    }
    time_runs = 1;
    /* Proper run. */
    for (int run = 0; run < num_runs; run++) {
        int r;
        
        assert(futhark_context_sync(ctx) == 0);
        t_start = get_wall_time();
        r = futhark_entry_main(ctx, &result_29317);
        if (r != 0)
            panic(1, "%s", futhark_context_get_error(ctx));
        assert(futhark_context_sync(ctx) == 0);
        t_end = get_wall_time();
        
        long elapsed_usec = t_end - t_start;
        
        if (time_runs && runtime_file != NULL)
            fprintf(runtime_file, "%lld\n", (long long) elapsed_usec);
        if (run < num_runs - 1) {
            ;
        }
    }
    write_scalar(stdout, binary_output, &bool_info, &result_29317);
    printf("\n");
    ;
}
typedef void entry_point_fun(struct futhark_context *);
struct entry_point_entry {
    const char *name;
    entry_point_fun *fun;
} ;
int main(int argc, char **argv)
{
    fut_progname = argv[0];
    
    struct entry_point_entry entry_points[] = {{.name ="main", .fun =
                                                futrts_cli_entry_main}};
    struct futhark_context_config *cfg = futhark_context_config_new();
    
    assert(cfg != NULL);
    
    int parsed_options = parse_options(cfg, argc, argv);
    
    argc -= parsed_options;
    argv += parsed_options;
    if (argc != 0)
        panic(1, "Excess non-option: %s\n", argv[0]);
    
    struct futhark_context *ctx = futhark_context_new(cfg);
    
    assert(ctx != NULL);
    
    int num_entry_points = sizeof(entry_points) / sizeof(entry_points[0]);
    entry_point_fun *entry_point_fun = NULL;
    
    for (int i = 0; i < num_entry_points; i++) {
        if (strcmp(entry_points[i].name, entry_point) == 0) {
            entry_point_fun = entry_points[i].fun;
            break;
        }
    }
    if (entry_point_fun == NULL) {
        fprintf(stderr,
                "No entry point '%s'.  Select another with --entry-point.  Options are:\n",
                entry_point);
        for (int i = 0; i < num_entry_points; i++)
            fprintf(stderr, "%s\n", entry_points[i].name);
        return 1;
    }
    entry_point_fun(ctx);
    if (runtime_file != NULL)
        fclose(runtime_file);
    futhark_debugging_report(ctx);
    futhark_context_free(ctx);
    futhark_context_config_free(cfg);
    return 0;
}
#ifdef _MSC_VER
#define inline __inline
#endif
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
/* A very simple cross-platform implementation of locks.  Uses
   pthreads on Unix and some Windows thing there.  Futhark's
   host-level code is not multithreaded, but user code may be, so we
   need some mechanism for ensuring atomic access to API functions.
   This is that mechanism.  It is not exposed to user code at all, so
   we do not have to worry about name collisions. */

#ifdef _WIN32

typedef HANDLE lock_t;

static lock_t create_lock(lock_t *lock) {
  *lock = CreateMutex(NULL,  /* Default security attributes. */
                      FALSE, /* Initially unlocked. */
                      NULL); /* Unnamed. */
}

static void lock_lock(lock_t *lock) {
  assert(WaitForSingleObject(*lock, INFINITE) == WAIT_OBJECT_0);
}

static void lock_unlock(lock_t *lock) {
  assert(ReleaseMutex(*lock));
}

static void free_lock(lock_t *lock) {
  CloseHandle(*lock);
}

#else
/* Assuming POSIX */

#include <pthread.h>

typedef pthread_mutex_t lock_t;

static void create_lock(lock_t *lock) {
  int r = pthread_mutex_init(lock, NULL);
  assert(r == 0);
}

static void lock_lock(lock_t *lock) {
  int r = pthread_mutex_lock(lock);
  assert(r == 0);
}

static void lock_unlock(lock_t *lock) {
  int r = pthread_mutex_unlock(lock);
  assert(r == 0);
}

static void free_lock(lock_t *lock) {
  /* Nothing to do for pthreads. */
  lock = lock;
}

#endif

static int32_t static_array_realtype_29308[4] = {1, 2, 3, 4};
static int32_t static_array_realtype_29309[4] = {0, 0, 1, 1};
static int32_t static_array_realtype_29310[4] = {0, 1, 0, 1};
static int32_t static_array_realtype_29311[3] = {0, 1, 1};
static int32_t static_array_realtype_29312[3] = {1, 0, 1};
static int32_t static_array_realtype_29313[3] = {1, 2, 3};
static int32_t static_array_realtype_29314[3] = {1, 4, 3};
static int32_t static_array_realtype_29315[4] = {0, 1, 1, 0};
static int32_t static_array_realtype_29316[4] = {1, 0, 1, 0};
struct memblock {
    int *references;
    char *mem;
    int64_t size;
    const char *desc;
} ;
struct futhark_context_config {
    int debugging;
} ;
struct futhark_context_config *futhark_context_config_new()
{
    struct futhark_context_config *cfg =
                                  malloc(sizeof(struct futhark_context_config));
    
    if (cfg == NULL)
        return NULL;
    cfg->debugging = 0;
    return cfg;
}
void futhark_context_config_free(struct futhark_context_config *cfg)
{
    free(cfg);
}
void futhark_context_config_set_debugging(struct futhark_context_config *cfg,
                                          int detail)
{
    cfg->debugging = detail;
}
void futhark_context_config_set_logging(struct futhark_context_config *cfg,
                                        int detail)
{
    /* Does nothing for this backend. */
    cfg = cfg;
    detail = detail;
}
struct futhark_context {
    int detail_memory;
    int debugging;
    lock_t lock;
    char *error;
    int64_t peak_mem_usage_default;
    int64_t cur_mem_usage_default;
    struct memblock static_array_29126;
    struct memblock static_array_29134;
    struct memblock static_array_29135;
    struct memblock static_array_29139;
    struct memblock static_array_29140;
    struct memblock static_array_29141;
    struct memblock static_array_29179;
    struct memblock static_array_29183;
    struct memblock static_array_29184;
} ;
struct futhark_context *futhark_context_new(struct futhark_context_config *cfg)
{
    struct futhark_context *ctx = malloc(sizeof(struct futhark_context));
    
    if (ctx == NULL)
        return NULL;
    ctx->detail_memory = cfg->debugging;
    ctx->debugging = cfg->debugging;
    ctx->error = NULL;
    create_lock(&ctx->lock);
    ctx->peak_mem_usage_default = 0;
    ctx->cur_mem_usage_default = 0;
    ctx->static_array_29126 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29308,
                                                 0};
    ctx->static_array_29134 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29309,
                                                 0};
    ctx->static_array_29135 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29310,
                                                 0};
    ctx->static_array_29139 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29311,
                                                 0};
    ctx->static_array_29140 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29312,
                                                 0};
    ctx->static_array_29141 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29313,
                                                 0};
    ctx->static_array_29179 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29314,
                                                 0};
    ctx->static_array_29183 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29315,
                                                 0};
    ctx->static_array_29184 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_29316,
                                                 0};
    return ctx;
}
void futhark_context_free(struct futhark_context *ctx)
{
    free_lock(&ctx->lock);
    free(ctx);
}
int futhark_context_sync(struct futhark_context *ctx)
{
    ctx = ctx;
    return 0;
}
char *futhark_context_get_error(struct futhark_context *ctx)
{
    char *error = ctx->error;
    
    ctx->error = NULL;
    return error;
}
static void memblock_unref(struct futhark_context *ctx, struct memblock *block,
                           const char *desc)
{
    if (block->references != NULL) {
        *block->references -= 1;
        if (ctx->detail_memory)
            fprintf(stderr,
                    "Unreferencing block %s (allocated as %s) in %s: %d references remaining.\n",
                    desc, block->desc, "default space", *block->references);
        if (*block->references == 0) {
            ctx->cur_mem_usage_default -= block->size;
            free(block->mem);
            free(block->references);
            if (ctx->detail_memory)
                fprintf(stderr,
                        "%lld bytes freed (now allocated: %lld bytes)\n",
                        (long long) block->size,
                        (long long) ctx->cur_mem_usage_default);
        }
        block->references = NULL;
    }
}
static void memblock_alloc(struct futhark_context *ctx, struct memblock *block,
                           int64_t size, const char *desc)
{
    if (size < 0)
        panic(1, "Negative allocation of %lld bytes attempted for %s in %s.\n",
              (long long) size, desc, "default space",
              ctx->cur_mem_usage_default);
    memblock_unref(ctx, block, desc);
    block->mem = (char *) malloc(size);
    block->references = (int *) malloc(sizeof(int));
    *block->references = 1;
    block->size = size;
    block->desc = desc;
    ctx->cur_mem_usage_default += size;
    if (ctx->detail_memory)
        fprintf(stderr,
                "Allocated %lld bytes for %s in %s (now allocated: %lld bytes)",
                (long long) size, desc, "default space",
                (long long) ctx->cur_mem_usage_default);
    if (ctx->cur_mem_usage_default > ctx->peak_mem_usage_default) {
        ctx->peak_mem_usage_default = ctx->cur_mem_usage_default;
        if (ctx->detail_memory)
            fprintf(stderr, " (new peak).\n");
    } else if (ctx->detail_memory)
        fprintf(stderr, ".\n");
}
static void memblock_set(struct futhark_context *ctx, struct memblock *lhs,
                         struct memblock *rhs, const char *lhs_desc)
{
    memblock_unref(ctx, lhs, lhs_desc);
    (*rhs->references)++;
    *lhs = *rhs;
}
void futhark_debugging_report(struct futhark_context *ctx)
{
    if (ctx->detail_memory) {
        fprintf(stderr, "Peak memory usage for default space: %lld bytes.\n",
                (long long) ctx->peak_mem_usage_default);
    }
    if (ctx->debugging) { }
}
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_29307);
static inline int8_t add8(int8_t x, int8_t y)
{
    return x + y;
}
static inline int16_t add16(int16_t x, int16_t y)
{
    return x + y;
}
static inline int32_t add32(int32_t x, int32_t y)
{
    return x + y;
}
static inline int64_t add64(int64_t x, int64_t y)
{
    return x + y;
}
static inline int8_t sub8(int8_t x, int8_t y)
{
    return x - y;
}
static inline int16_t sub16(int16_t x, int16_t y)
{
    return x - y;
}
static inline int32_t sub32(int32_t x, int32_t y)
{
    return x - y;
}
static inline int64_t sub64(int64_t x, int64_t y)
{
    return x - y;
}
static inline int8_t mul8(int8_t x, int8_t y)
{
    return x * y;
}
static inline int16_t mul16(int16_t x, int16_t y)
{
    return x * y;
}
static inline int32_t mul32(int32_t x, int32_t y)
{
    return x * y;
}
static inline int64_t mul64(int64_t x, int64_t y)
{
    return x * y;
}
static inline uint8_t udiv8(uint8_t x, uint8_t y)
{
    return x / y;
}
static inline uint16_t udiv16(uint16_t x, uint16_t y)
{
    return x / y;
}
static inline uint32_t udiv32(uint32_t x, uint32_t y)
{
    return x / y;
}
static inline uint64_t udiv64(uint64_t x, uint64_t y)
{
    return x / y;
}
static inline uint8_t umod8(uint8_t x, uint8_t y)
{
    return x % y;
}
static inline uint16_t umod16(uint16_t x, uint16_t y)
{
    return x % y;
}
static inline uint32_t umod32(uint32_t x, uint32_t y)
{
    return x % y;
}
static inline uint64_t umod64(uint64_t x, uint64_t y)
{
    return x % y;
}
static inline int8_t sdiv8(int8_t x, int8_t y)
{
    int8_t q = x / y;
    int8_t r = x % y;
    
    return q - ((r != 0 && r < 0 != y < 0) ? 1 : 0);
}
static inline int16_t sdiv16(int16_t x, int16_t y)
{
    int16_t q = x / y;
    int16_t r = x % y;
    
    return q - ((r != 0 && r < 0 != y < 0) ? 1 : 0);
}
static inline int32_t sdiv32(int32_t x, int32_t y)
{
    int32_t q = x / y;
    int32_t r = x % y;
    
    return q - ((r != 0 && r < 0 != y < 0) ? 1 : 0);
}
static inline int64_t sdiv64(int64_t x, int64_t y)
{
    int64_t q = x / y;
    int64_t r = x % y;
    
    return q - ((r != 0 && r < 0 != y < 0) ? 1 : 0);
}
static inline int8_t smod8(int8_t x, int8_t y)
{
    int8_t r = x % y;
    
    return r + (r == 0 || (x > 0 && y > 0) || (x < 0 && y < 0) ? 0 : y);
}
static inline int16_t smod16(int16_t x, int16_t y)
{
    int16_t r = x % y;
    
    return r + (r == 0 || (x > 0 && y > 0) || (x < 0 && y < 0) ? 0 : y);
}
static inline int32_t smod32(int32_t x, int32_t y)
{
    int32_t r = x % y;
    
    return r + (r == 0 || (x > 0 && y > 0) || (x < 0 && y < 0) ? 0 : y);
}
static inline int64_t smod64(int64_t x, int64_t y)
{
    int64_t r = x % y;
    
    return r + (r == 0 || (x > 0 && y > 0) || (x < 0 && y < 0) ? 0 : y);
}
static inline int8_t squot8(int8_t x, int8_t y)
{
    return x / y;
}
static inline int16_t squot16(int16_t x, int16_t y)
{
    return x / y;
}
static inline int32_t squot32(int32_t x, int32_t y)
{
    return x / y;
}
static inline int64_t squot64(int64_t x, int64_t y)
{
    return x / y;
}
static inline int8_t srem8(int8_t x, int8_t y)
{
    return x % y;
}
static inline int16_t srem16(int16_t x, int16_t y)
{
    return x % y;
}
static inline int32_t srem32(int32_t x, int32_t y)
{
    return x % y;
}
static inline int64_t srem64(int64_t x, int64_t y)
{
    return x % y;
}
static inline int8_t smin8(int8_t x, int8_t y)
{
    return x < y ? x : y;
}
static inline int16_t smin16(int16_t x, int16_t y)
{
    return x < y ? x : y;
}
static inline int32_t smin32(int32_t x, int32_t y)
{
    return x < y ? x : y;
}
static inline int64_t smin64(int64_t x, int64_t y)
{
    return x < y ? x : y;
}
static inline uint8_t umin8(uint8_t x, uint8_t y)
{
    return x < y ? x : y;
}
static inline uint16_t umin16(uint16_t x, uint16_t y)
{
    return x < y ? x : y;
}
static inline uint32_t umin32(uint32_t x, uint32_t y)
{
    return x < y ? x : y;
}
static inline uint64_t umin64(uint64_t x, uint64_t y)
{
    return x < y ? x : y;
}
static inline int8_t smax8(int8_t x, int8_t y)
{
    return x < y ? y : x;
}
static inline int16_t smax16(int16_t x, int16_t y)
{
    return x < y ? y : x;
}
static inline int32_t smax32(int32_t x, int32_t y)
{
    return x < y ? y : x;
}
static inline int64_t smax64(int64_t x, int64_t y)
{
    return x < y ? y : x;
}
static inline uint8_t umax8(uint8_t x, uint8_t y)
{
    return x < y ? y : x;
}
static inline uint16_t umax16(uint16_t x, uint16_t y)
{
    return x < y ? y : x;
}
static inline uint32_t umax32(uint32_t x, uint32_t y)
{
    return x < y ? y : x;
}
static inline uint64_t umax64(uint64_t x, uint64_t y)
{
    return x < y ? y : x;
}
static inline uint8_t shl8(uint8_t x, uint8_t y)
{
    return x << y;
}
static inline uint16_t shl16(uint16_t x, uint16_t y)
{
    return x << y;
}
static inline uint32_t shl32(uint32_t x, uint32_t y)
{
    return x << y;
}
static inline uint64_t shl64(uint64_t x, uint64_t y)
{
    return x << y;
}
static inline uint8_t lshr8(uint8_t x, uint8_t y)
{
    return x >> y;
}
static inline uint16_t lshr16(uint16_t x, uint16_t y)
{
    return x >> y;
}
static inline uint32_t lshr32(uint32_t x, uint32_t y)
{
    return x >> y;
}
static inline uint64_t lshr64(uint64_t x, uint64_t y)
{
    return x >> y;
}
static inline int8_t ashr8(int8_t x, int8_t y)
{
    return x >> y;
}
static inline int16_t ashr16(int16_t x, int16_t y)
{
    return x >> y;
}
static inline int32_t ashr32(int32_t x, int32_t y)
{
    return x >> y;
}
static inline int64_t ashr64(int64_t x, int64_t y)
{
    return x >> y;
}
static inline uint8_t and8(uint8_t x, uint8_t y)
{
    return x & y;
}
static inline uint16_t and16(uint16_t x, uint16_t y)
{
    return x & y;
}
static inline uint32_t and32(uint32_t x, uint32_t y)
{
    return x & y;
}
static inline uint64_t and64(uint64_t x, uint64_t y)
{
    return x & y;
}
static inline uint8_t or8(uint8_t x, uint8_t y)
{
    return x | y;
}
static inline uint16_t or16(uint16_t x, uint16_t y)
{
    return x | y;
}
static inline uint32_t or32(uint32_t x, uint32_t y)
{
    return x | y;
}
static inline uint64_t or64(uint64_t x, uint64_t y)
{
    return x | y;
}
static inline uint8_t xor8(uint8_t x, uint8_t y)
{
    return x ^ y;
}
static inline uint16_t xor16(uint16_t x, uint16_t y)
{
    return x ^ y;
}
static inline uint32_t xor32(uint32_t x, uint32_t y)
{
    return x ^ y;
}
static inline uint64_t xor64(uint64_t x, uint64_t y)
{
    return x ^ y;
}
static inline char ult8(uint8_t x, uint8_t y)
{
    return x < y;
}
static inline char ult16(uint16_t x, uint16_t y)
{
    return x < y;
}
static inline char ult32(uint32_t x, uint32_t y)
{
    return x < y;
}
static inline char ult64(uint64_t x, uint64_t y)
{
    return x < y;
}
static inline char ule8(uint8_t x, uint8_t y)
{
    return x <= y;
}
static inline char ule16(uint16_t x, uint16_t y)
{
    return x <= y;
}
static inline char ule32(uint32_t x, uint32_t y)
{
    return x <= y;
}
static inline char ule64(uint64_t x, uint64_t y)
{
    return x <= y;
}
static inline char slt8(int8_t x, int8_t y)
{
    return x < y;
}
static inline char slt16(int16_t x, int16_t y)
{
    return x < y;
}
static inline char slt32(int32_t x, int32_t y)
{
    return x < y;
}
static inline char slt64(int64_t x, int64_t y)
{
    return x < y;
}
static inline char sle8(int8_t x, int8_t y)
{
    return x <= y;
}
static inline char sle16(int16_t x, int16_t y)
{
    return x <= y;
}
static inline char sle32(int32_t x, int32_t y)
{
    return x <= y;
}
static inline char sle64(int64_t x, int64_t y)
{
    return x <= y;
}
static inline int8_t pow8(int8_t x, int8_t y)
{
    int8_t res = 1, rem = y;
    
    while (rem != 0) {
        if (rem & 1)
            res *= x;
        rem >>= 1;
        x *= x;
    }
    return res;
}
static inline int16_t pow16(int16_t x, int16_t y)
{
    int16_t res = 1, rem = y;
    
    while (rem != 0) {
        if (rem & 1)
            res *= x;
        rem >>= 1;
        x *= x;
    }
    return res;
}
static inline int32_t pow32(int32_t x, int32_t y)
{
    int32_t res = 1, rem = y;
    
    while (rem != 0) {
        if (rem & 1)
            res *= x;
        rem >>= 1;
        x *= x;
    }
    return res;
}
static inline int64_t pow64(int64_t x, int64_t y)
{
    int64_t res = 1, rem = y;
    
    while (rem != 0) {
        if (rem & 1)
            res *= x;
        rem >>= 1;
        x *= x;
    }
    return res;
}
static inline int8_t sext_i8_i8(int8_t x)
{
    return x;
}
static inline int16_t sext_i8_i16(int8_t x)
{
    return x;
}
static inline int32_t sext_i8_i32(int8_t x)
{
    return x;
}
static inline int64_t sext_i8_i64(int8_t x)
{
    return x;
}
static inline int8_t sext_i16_i8(int16_t x)
{
    return x;
}
static inline int16_t sext_i16_i16(int16_t x)
{
    return x;
}
static inline int32_t sext_i16_i32(int16_t x)
{
    return x;
}
static inline int64_t sext_i16_i64(int16_t x)
{
    return x;
}
static inline int8_t sext_i32_i8(int32_t x)
{
    return x;
}
static inline int16_t sext_i32_i16(int32_t x)
{
    return x;
}
static inline int32_t sext_i32_i32(int32_t x)
{
    return x;
}
static inline int64_t sext_i32_i64(int32_t x)
{
    return x;
}
static inline int8_t sext_i64_i8(int64_t x)
{
    return x;
}
static inline int16_t sext_i64_i16(int64_t x)
{
    return x;
}
static inline int32_t sext_i64_i32(int64_t x)
{
    return x;
}
static inline int64_t sext_i64_i64(int64_t x)
{
    return x;
}
static inline uint8_t zext_i8_i8(uint8_t x)
{
    return x;
}
static inline uint16_t zext_i8_i16(uint8_t x)
{
    return x;
}
static inline uint32_t zext_i8_i32(uint8_t x)
{
    return x;
}
static inline uint64_t zext_i8_i64(uint8_t x)
{
    return x;
}
static inline uint8_t zext_i16_i8(uint16_t x)
{
    return x;
}
static inline uint16_t zext_i16_i16(uint16_t x)
{
    return x;
}
static inline uint32_t zext_i16_i32(uint16_t x)
{
    return x;
}
static inline uint64_t zext_i16_i64(uint16_t x)
{
    return x;
}
static inline uint8_t zext_i32_i8(uint32_t x)
{
    return x;
}
static inline uint16_t zext_i32_i16(uint32_t x)
{
    return x;
}
static inline uint32_t zext_i32_i32(uint32_t x)
{
    return x;
}
static inline uint64_t zext_i32_i64(uint32_t x)
{
    return x;
}
static inline uint8_t zext_i64_i8(uint64_t x)
{
    return x;
}
static inline uint16_t zext_i64_i16(uint64_t x)
{
    return x;
}
static inline uint32_t zext_i64_i32(uint64_t x)
{
    return x;
}
static inline uint64_t zext_i64_i64(uint64_t x)
{
    return x;
}
static inline float fdiv32(float x, float y)
{
    return x / y;
}
static inline float fadd32(float x, float y)
{
    return x + y;
}
static inline float fsub32(float x, float y)
{
    return x - y;
}
static inline float fmul32(float x, float y)
{
    return x * y;
}
static inline float fmin32(float x, float y)
{
    return x < y ? x : y;
}
static inline float fmax32(float x, float y)
{
    return x < y ? y : x;
}
static inline float fpow32(float x, float y)
{
    return pow(x, y);
}
static inline char cmplt32(float x, float y)
{
    return x < y;
}
static inline char cmple32(float x, float y)
{
    return x <= y;
}
static inline float sitofp_i8_f32(int8_t x)
{
    return x;
}
static inline float sitofp_i16_f32(int16_t x)
{
    return x;
}
static inline float sitofp_i32_f32(int32_t x)
{
    return x;
}
static inline float sitofp_i64_f32(int64_t x)
{
    return x;
}
static inline float uitofp_i8_f32(uint8_t x)
{
    return x;
}
static inline float uitofp_i16_f32(uint16_t x)
{
    return x;
}
static inline float uitofp_i32_f32(uint32_t x)
{
    return x;
}
static inline float uitofp_i64_f32(uint64_t x)
{
    return x;
}
static inline int8_t fptosi_f32_i8(float x)
{
    return x;
}
static inline int16_t fptosi_f32_i16(float x)
{
    return x;
}
static inline int32_t fptosi_f32_i32(float x)
{
    return x;
}
static inline int64_t fptosi_f32_i64(float x)
{
    return x;
}
static inline uint8_t fptoui_f32_i8(float x)
{
    return x;
}
static inline uint16_t fptoui_f32_i16(float x)
{
    return x;
}
static inline uint32_t fptoui_f32_i32(float x)
{
    return x;
}
static inline uint64_t fptoui_f32_i64(float x)
{
    return x;
}
static inline double fdiv64(double x, double y)
{
    return x / y;
}
static inline double fadd64(double x, double y)
{
    return x + y;
}
static inline double fsub64(double x, double y)
{
    return x - y;
}
static inline double fmul64(double x, double y)
{
    return x * y;
}
static inline double fmin64(double x, double y)
{
    return x < y ? x : y;
}
static inline double fmax64(double x, double y)
{
    return x < y ? y : x;
}
static inline double fpow64(double x, double y)
{
    return pow(x, y);
}
static inline char cmplt64(double x, double y)
{
    return x < y;
}
static inline char cmple64(double x, double y)
{
    return x <= y;
}
static inline double sitofp_i8_f64(int8_t x)
{
    return x;
}
static inline double sitofp_i16_f64(int16_t x)
{
    return x;
}
static inline double sitofp_i32_f64(int32_t x)
{
    return x;
}
static inline double sitofp_i64_f64(int64_t x)
{
    return x;
}
static inline double uitofp_i8_f64(uint8_t x)
{
    return x;
}
static inline double uitofp_i16_f64(uint16_t x)
{
    return x;
}
static inline double uitofp_i32_f64(uint32_t x)
{
    return x;
}
static inline double uitofp_i64_f64(uint64_t x)
{
    return x;
}
static inline int8_t fptosi_f64_i8(double x)
{
    return x;
}
static inline int16_t fptosi_f64_i16(double x)
{
    return x;
}
static inline int32_t fptosi_f64_i32(double x)
{
    return x;
}
static inline int64_t fptosi_f64_i64(double x)
{
    return x;
}
static inline uint8_t fptoui_f64_i8(double x)
{
    return x;
}
static inline uint16_t fptoui_f64_i16(double x)
{
    return x;
}
static inline uint32_t fptoui_f64_i32(double x)
{
    return x;
}
static inline uint64_t fptoui_f64_i64(double x)
{
    return x;
}
static inline float fpconv_f32_f32(float x)
{
    return x;
}
static inline double fpconv_f32_f64(float x)
{
    return x;
}
static inline float fpconv_f64_f32(double x)
{
    return x;
}
static inline double fpconv_f64_f64(double x)
{
    return x;
}
static inline float futrts_log32(float x)
{
    return log(x);
}
static inline float futrts_log2_32(float x)
{
    return log2(x);
}
static inline float futrts_log10_32(float x)
{
    return log10(x);
}
static inline float futrts_sqrt32(float x)
{
    return sqrt(x);
}
static inline float futrts_exp32(float x)
{
    return exp(x);
}
static inline float futrts_cos32(float x)
{
    return cos(x);
}
static inline float futrts_sin32(float x)
{
    return sin(x);
}
static inline float futrts_tan32(float x)
{
    return tan(x);
}
static inline float futrts_acos32(float x)
{
    return acos(x);
}
static inline float futrts_asin32(float x)
{
    return asin(x);
}
static inline float futrts_atan32(float x)
{
    return atan(x);
}
static inline float futrts_atan2_32(float x, float y)
{
    return atan2(x, y);
}
static inline float futrts_round32(float x)
{
    return rint(x);
}
static inline char futrts_isnan32(float x)
{
    return isnan(x);
}
static inline char futrts_isinf32(float x)
{
    return isinf(x);
}
static inline int32_t futrts_to_bits32(float x)
{
    union {
        float f;
        int32_t t;
    } p;
    
    p.f = x;
    return p.t;
}
static inline float futrts_from_bits32(int32_t x)
{
    union {
        int32_t f;
        float t;
    } p;
    
    p.f = x;
    return p.t;
}
static inline double futrts_log64(double x)
{
    return log(x);
}
static inline double futrts_log2_64(double x)
{
    return log2(x);
}
static inline double futrts_log10_64(double x)
{
    return log10(x);
}
static inline double futrts_sqrt64(double x)
{
    return sqrt(x);
}
static inline double futrts_exp64(double x)
{
    return exp(x);
}
static inline double futrts_cos64(double x)
{
    return cos(x);
}
static inline double futrts_sin64(double x)
{
    return sin(x);
}
static inline double futrts_tan64(double x)
{
    return tan(x);
}
static inline double futrts_acos64(double x)
{
    return acos(x);
}
static inline double futrts_asin64(double x)
{
    return asin(x);
}
static inline double futrts_atan64(double x)
{
    return atan(x);
}
static inline double futrts_atan2_64(double x, double y)
{
    return atan2(x, y);
}
static inline double futrts_round64(double x)
{
    return rint(x);
}
static inline char futrts_isnan64(double x)
{
    return isnan(x);
}
static inline char futrts_isinf64(double x)
{
    return isinf(x);
}
static inline int64_t futrts_to_bits64(double x)
{
    union {
        double f;
        int64_t t;
    } p;
    
    p.f = x;
    return p.t;
}
static inline double futrts_from_bits64(int64_t x)
{
    union {
        int64_t f;
        double t;
    } p;
    
    p.f = x;
    return p.t;
}
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_29307)
{
    bool scalar_out_29125;
    struct memblock mem_28309;
    
    mem_28309.references = NULL;
    memblock_alloc(ctx, &mem_28309, 16, "mem_28309");
    
    struct memblock static_array_29126 = ctx->static_array_29126;
    
    memmove(mem_28309.mem + 0, static_array_29126.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_28312;
    
    mem_28312.references = NULL;
    memblock_alloc(ctx, &mem_28312, 16, "mem_28312");
    
    struct memblock mem_28317;
    
    mem_28317.references = NULL;
    memblock_alloc(ctx, &mem_28317, 8, "mem_28317");
    for (int32_t i_27430 = 0; i_27430 < 2; i_27430++) {
        for (int32_t i_29128 = 0; i_29128 < 2; i_29128++) {
            *(int32_t *) &mem_28317.mem[i_29128 * 4] = i_27430;
        }
        memmove(mem_28312.mem + 2 * i_27430 * 4, mem_28317.mem + 0, 2 *
                sizeof(int32_t));
    }
    memblock_unref(ctx, &mem_28317, "mem_28317");
    
    struct memblock mem_28322;
    
    mem_28322.references = NULL;
    memblock_alloc(ctx, &mem_28322, 16, "mem_28322");
    
    int32_t discard_27437;
    int32_t scanacc_27433 = 0;
    
    for (int32_t i_27435 = 0; i_27435 < 4; i_27435++) {
        int32_t zz_26339 = 1 + scanacc_27433;
        
        *(int32_t *) &mem_28322.mem[i_27435 * 4] = zz_26339;
        
        int32_t scanacc_tmp_29129 = zz_26339;
        
        scanacc_27433 = scanacc_tmp_29129;
    }
    discard_27437 = scanacc_27433;
    
    int32_t last_offset_26341 = *(int32_t *) &mem_28322.mem[12];
    int64_t binop_x_28328 = sext_i32_i64(last_offset_26341);
    int64_t bytes_28327 = 4 * binop_x_28328;
    struct memblock mem_28329;
    
    mem_28329.references = NULL;
    memblock_alloc(ctx, &mem_28329, bytes_28327, "mem_28329");
    
    struct memblock mem_28332;
    
    mem_28332.references = NULL;
    memblock_alloc(ctx, &mem_28332, bytes_28327, "mem_28332");
    
    struct memblock mem_28335;
    
    mem_28335.references = NULL;
    memblock_alloc(ctx, &mem_28335, bytes_28327, "mem_28335");
    for (int32_t write_iter_27438 = 0; write_iter_27438 < 4;
         write_iter_27438++) {
        int32_t write_iv_27442 = *(int32_t *) &mem_28322.mem[write_iter_27438 *
                                                             4];
        int32_t new_index_28077 = squot32(write_iter_27438, 2);
        int32_t binop_y_28079 = 2 * new_index_28077;
        int32_t new_index_28080 = write_iter_27438 - binop_y_28079;
        int32_t write_iv_27443 = *(int32_t *) &mem_28312.mem[(new_index_28077 *
                                                              2 +
                                                              new_index_28080) *
                                                             4];
        int32_t write_iv_27445 = *(int32_t *) &mem_28309.mem[write_iter_27438 *
                                                             4];
        int32_t this_offset_26352 = -1 + write_iv_27442;
        bool less_than_zzero_27446 = slt32(this_offset_26352, 0);
        bool greater_than_sizze_27447 = sle32(last_offset_26341,
                                              this_offset_26352);
        bool outside_bounds_dim_27448 = less_than_zzero_27446 ||
             greater_than_sizze_27447;
        
        if (!outside_bounds_dim_27448) {
            *(int32_t *) &mem_28329.mem[this_offset_26352 * 4] = write_iv_27443;
        }
        if (!outside_bounds_dim_27448) {
            *(int32_t *) &mem_28332.mem[this_offset_26352 * 4] =
                new_index_28080;
        }
        if (!outside_bounds_dim_27448) {
            *(int32_t *) &mem_28335.mem[this_offset_26352 * 4] = write_iv_27445;
        }
    }
    memblock_unref(ctx, &mem_28312, "mem_28312");
    memblock_unref(ctx, &mem_28322, "mem_28322");
    
    int32_t x_26353 = abs(last_offset_26341);
    bool empty_slice_26354 = x_26353 == 0;
    int32_t m_26355 = x_26353 - 1;
    bool zzero_leq_i_p_m_t_s_26356 = sle32(0, m_26355);
    bool i_p_m_t_s_leq_w_26357 = slt32(m_26355, last_offset_26341);
    bool y_26358 = zzero_leq_i_p_m_t_s_26356 && i_p_m_t_s_leq_w_26357;
    bool ok_or_empty_26359 = empty_slice_26354 || y_26358;
    bool index_certs_26360;
    
    if (!ok_or_empty_26359) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:157:14-157:18 -> tupleTest.fut:17:13-17:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", last_offset_26341,
                               "] out of bounds for array of shape [",
                               last_offset_26341, "].");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    struct memblock mem_28356;
    
    mem_28356.references = NULL;
    memblock_alloc(ctx, &mem_28356, 16, "mem_28356");
    
    struct memblock static_array_29134 = ctx->static_array_29134;
    
    memmove(mem_28356.mem + 0, static_array_29134.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_28359;
    
    mem_28359.references = NULL;
    memblock_alloc(ctx, &mem_28359, 16, "mem_28359");
    
    struct memblock static_array_29135 = ctx->static_array_29135;
    
    memmove(mem_28359.mem + 0, static_array_29135.mem + 0, 4 * sizeof(int32_t));
    
    bool dim_eq_26366 = x_26353 == 4;
    bool arrays_equal_26367;
    
    if (dim_eq_26366) {
        bool all_equal_26369;
        bool redout_27464 = 1;
        
        for (int32_t i_27465 = 0; i_27465 < x_26353; i_27465++) {
            int32_t x_26373 = *(int32_t *) &mem_28329.mem[i_27465 * 4];
            int32_t y_26374 = *(int32_t *) &mem_28356.mem[i_27465 * 4];
            bool res_26375 = x_26373 == y_26374;
            bool res_26372 = res_26375 && redout_27464;
            bool redout_tmp_29136 = res_26372;
            
            redout_27464 = redout_tmp_29136;
        }
        all_equal_26369 = redout_27464;
        arrays_equal_26367 = all_equal_26369;
    } else {
        arrays_equal_26367 = 0;
    }
    memblock_unref(ctx, &mem_28329, "mem_28329");
    memblock_unref(ctx, &mem_28356, "mem_28356");
    
    bool arrays_equal_26376;
    
    if (dim_eq_26366) {
        bool all_equal_26378;
        bool redout_27466 = 1;
        
        for (int32_t i_27467 = 0; i_27467 < x_26353; i_27467++) {
            int32_t x_26382 = *(int32_t *) &mem_28332.mem[i_27467 * 4];
            int32_t y_26383 = *(int32_t *) &mem_28359.mem[i_27467 * 4];
            bool res_26384 = x_26382 == y_26383;
            bool res_26381 = res_26384 && redout_27466;
            bool redout_tmp_29137 = res_26381;
            
            redout_27466 = redout_tmp_29137;
        }
        all_equal_26378 = redout_27466;
        arrays_equal_26376 = all_equal_26378;
    } else {
        arrays_equal_26376 = 0;
    }
    memblock_unref(ctx, &mem_28332, "mem_28332");
    memblock_unref(ctx, &mem_28359, "mem_28359");
    
    bool eq_26385 = arrays_equal_26367 && arrays_equal_26376;
    bool res_26386;
    
    if (eq_26385) {
        bool arrays_equal_26387;
        
        if (dim_eq_26366) {
            bool all_equal_26389;
            bool redout_27468 = 1;
            
            for (int32_t i_27469 = 0; i_27469 < x_26353; i_27469++) {
                int32_t x_26393 = *(int32_t *) &mem_28335.mem[i_27469 * 4];
                int32_t y_26394 = *(int32_t *) &mem_28309.mem[i_27469 * 4];
                bool res_26395 = x_26393 == y_26394;
                bool res_26392 = res_26395 && redout_27468;
                bool redout_tmp_29138 = res_26392;
                
                redout_27468 = redout_tmp_29138;
            }
            all_equal_26389 = redout_27468;
            arrays_equal_26387 = all_equal_26389;
        } else {
            arrays_equal_26387 = 0;
        }
        res_26386 = arrays_equal_26387;
    } else {
        res_26386 = 0;
    }
    memblock_unref(ctx, &mem_28335, "mem_28335");
    
    struct memblock mem_28362;
    
    mem_28362.references = NULL;
    memblock_alloc(ctx, &mem_28362, 12, "mem_28362");
    
    struct memblock static_array_29139 = ctx->static_array_29139;
    
    memmove(mem_28362.mem + 0, static_array_29139.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_28365;
    
    mem_28365.references = NULL;
    memblock_alloc(ctx, &mem_28365, 12, "mem_28365");
    
    struct memblock static_array_29140 = ctx->static_array_29140;
    
    memmove(mem_28365.mem + 0, static_array_29140.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_28368;
    
    mem_28368.references = NULL;
    memblock_alloc(ctx, &mem_28368, 12, "mem_28368");
    
    struct memblock static_array_29141 = ctx->static_array_29141;
    
    memmove(mem_28368.mem + 0, static_array_29141.mem + 0, 3 * sizeof(int32_t));
    
    bool cond_26400;
    
    if (res_26386) {
        struct memblock mem_28371;
        
        mem_28371.references = NULL;
        memblock_alloc(ctx, &mem_28371, 16, "mem_28371");
        
        struct memblock mem_28376;
        
        mem_28376.references = NULL;
        memblock_alloc(ctx, &mem_28376, 8, "mem_28376");
        for (int32_t i_27472 = 0; i_27472 < 2; i_27472++) {
            for (int32_t i_29143 = 0; i_29143 < 2; i_29143++) {
                *(int32_t *) &mem_28376.mem[i_29143 * 4] = i_27472;
            }
            memmove(mem_28371.mem + 2 * i_27472 * 4, mem_28376.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_28376, "mem_28376");
        
        struct memblock mem_28381;
        
        mem_28381.references = NULL;
        memblock_alloc(ctx, &mem_28381, 16, "mem_28381");
        
        struct memblock mem_28384;
        
        mem_28384.references = NULL;
        memblock_alloc(ctx, &mem_28384, 16, "mem_28384");
        
        int32_t discard_27482;
        int32_t scanacc_27476 = 0;
        
        for (int32_t i_27479 = 0; i_27479 < 4; i_27479++) {
            bool not_arg_26411 = i_27479 == 0;
            bool res_26412 = !not_arg_26411;
            int32_t part_res_26413;
            
            if (res_26412) {
                part_res_26413 = 0;
            } else {
                part_res_26413 = 1;
            }
            
            int32_t part_res_26414;
            
            if (res_26412) {
                part_res_26414 = 1;
            } else {
                part_res_26414 = 0;
            }
            
            int32_t zz_26409 = part_res_26414 + scanacc_27476;
            
            *(int32_t *) &mem_28381.mem[i_27479 * 4] = zz_26409;
            *(int32_t *) &mem_28384.mem[i_27479 * 4] = part_res_26413;
            
            int32_t scanacc_tmp_29144 = zz_26409;
            
            scanacc_27476 = scanacc_tmp_29144;
        }
        discard_27482 = scanacc_27476;
        
        int32_t last_offset_26415 = *(int32_t *) &mem_28381.mem[12];
        int64_t binop_x_28394 = sext_i32_i64(last_offset_26415);
        int64_t bytes_28393 = 4 * binop_x_28394;
        struct memblock mem_28395;
        
        mem_28395.references = NULL;
        memblock_alloc(ctx, &mem_28395, bytes_28393, "mem_28395");
        
        struct memblock mem_28398;
        
        mem_28398.references = NULL;
        memblock_alloc(ctx, &mem_28398, bytes_28393, "mem_28398");
        
        struct memblock mem_28401;
        
        mem_28401.references = NULL;
        memblock_alloc(ctx, &mem_28401, bytes_28393, "mem_28401");
        for (int32_t write_iter_27483 = 0; write_iter_27483 < 4;
             write_iter_27483++) {
            int32_t write_iv_27487 =
                    *(int32_t *) &mem_28384.mem[write_iter_27483 * 4];
            int32_t write_iv_27488 =
                    *(int32_t *) &mem_28381.mem[write_iter_27483 * 4];
            int32_t new_index_28094 = squot32(write_iter_27483, 2);
            int32_t binop_y_28096 = 2 * new_index_28094;
            int32_t new_index_28097 = write_iter_27483 - binop_y_28096;
            int32_t write_iv_27489 =
                    *(int32_t *) &mem_28371.mem[(new_index_28094 * 2 +
                                                 new_index_28097) * 4];
            bool is_this_one_26427 = write_iv_27487 == 0;
            int32_t this_offset_26428 = -1 + write_iv_27488;
            int32_t total_res_26429;
            
            if (is_this_one_26427) {
                total_res_26429 = this_offset_26428;
            } else {
                total_res_26429 = -1;
            }
            
            bool less_than_zzero_27492 = slt32(total_res_26429, 0);
            bool greater_than_sizze_27493 = sle32(last_offset_26415,
                                                  total_res_26429);
            bool outside_bounds_dim_27494 = less_than_zzero_27492 ||
                 greater_than_sizze_27493;
            
            if (!outside_bounds_dim_27494) {
                *(int32_t *) &mem_28395.mem[total_res_26429 * 4] =
                    write_iv_27489;
            }
            if (!outside_bounds_dim_27494) {
                *(int32_t *) &mem_28398.mem[total_res_26429 * 4] =
                    new_index_28097;
            }
            if (!outside_bounds_dim_27494) {
                *(int32_t *) &mem_28401.mem[total_res_26429 * 4] =
                    write_iter_27483;
            }
        }
        memblock_unref(ctx, &mem_28371, "mem_28371");
        memblock_unref(ctx, &mem_28381, "mem_28381");
        memblock_unref(ctx, &mem_28384, "mem_28384");
        
        int32_t x_26430 = abs(last_offset_26415);
        bool empty_slice_26431 = x_26430 == 0;
        int32_t m_26432 = x_26430 - 1;
        bool zzero_leq_i_p_m_t_s_26433 = sle32(0, m_26432);
        bool i_p_m_t_s_leq_w_26434 = slt32(m_26432, last_offset_26415);
        bool y_26435 = zzero_leq_i_p_m_t_s_26433 && i_p_m_t_s_leq_w_26434;
        bool ok_or_empty_26436 = empty_slice_26431 || y_26435;
        bool index_certs_26437;
        
        if (!ok_or_empty_26436) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:157:23-157:27 -> tupleTest.fut:23:13-23:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_26415,
                                   "] out of bounds for array of shape [",
                                   last_offset_26415, "].");
            memblock_unref(ctx, &mem_28401, "mem_28401");
            memblock_unref(ctx, &mem_28398, "mem_28398");
            memblock_unref(ctx, &mem_28395, "mem_28395");
            memblock_unref(ctx, &mem_28384, "mem_28384");
            memblock_unref(ctx, &mem_28381, "mem_28381");
            memblock_unref(ctx, &mem_28376, "mem_28376");
            memblock_unref(ctx, &mem_28371, "mem_28371");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        bool dim_eq_26441 = x_26430 == 3;
        bool arrays_equal_26442;
        
        if (dim_eq_26441) {
            bool all_equal_26444;
            bool redout_27510 = 1;
            
            for (int32_t i_27511 = 0; i_27511 < x_26430; i_27511++) {
                int32_t x_26448 = *(int32_t *) &mem_28395.mem[i_27511 * 4];
                int32_t y_26449 = *(int32_t *) &mem_28362.mem[i_27511 * 4];
                bool res_26450 = x_26448 == y_26449;
                bool res_26447 = res_26450 && redout_27510;
                bool redout_tmp_29150 = res_26447;
                
                redout_27510 = redout_tmp_29150;
            }
            all_equal_26444 = redout_27510;
            arrays_equal_26442 = all_equal_26444;
        } else {
            arrays_equal_26442 = 0;
        }
        memblock_unref(ctx, &mem_28395, "mem_28395");
        
        bool arrays_equal_26451;
        
        if (dim_eq_26441) {
            bool all_equal_26453;
            bool redout_27512 = 1;
            
            for (int32_t i_27513 = 0; i_27513 < x_26430; i_27513++) {
                int32_t x_26457 = *(int32_t *) &mem_28398.mem[i_27513 * 4];
                int32_t y_26458 = *(int32_t *) &mem_28365.mem[i_27513 * 4];
                bool res_26459 = x_26457 == y_26458;
                bool res_26456 = res_26459 && redout_27512;
                bool redout_tmp_29151 = res_26456;
                
                redout_27512 = redout_tmp_29151;
            }
            all_equal_26453 = redout_27512;
            arrays_equal_26451 = all_equal_26453;
        } else {
            arrays_equal_26451 = 0;
        }
        memblock_unref(ctx, &mem_28398, "mem_28398");
        
        bool eq_26460 = arrays_equal_26442 && arrays_equal_26451;
        bool res_26461;
        
        if (eq_26460) {
            bool arrays_equal_26462;
            
            if (dim_eq_26441) {
                bool all_equal_26464;
                bool redout_27514 = 1;
                
                for (int32_t i_27515 = 0; i_27515 < x_26430; i_27515++) {
                    int32_t x_26468 = *(int32_t *) &mem_28401.mem[i_27515 * 4];
                    int32_t y_26469 = *(int32_t *) &mem_28368.mem[i_27515 * 4];
                    bool res_26470 = x_26468 == y_26469;
                    bool res_26467 = res_26470 && redout_27514;
                    bool redout_tmp_29152 = res_26467;
                    
                    redout_27514 = redout_tmp_29152;
                }
                all_equal_26464 = redout_27514;
                arrays_equal_26462 = all_equal_26464;
            } else {
                arrays_equal_26462 = 0;
            }
            res_26461 = arrays_equal_26462;
        } else {
            res_26461 = 0;
        }
        memblock_unref(ctx, &mem_28401, "mem_28401");
        cond_26400 = res_26461;
        memblock_unref(ctx, &mem_28401, "mem_28401");
        memblock_unref(ctx, &mem_28398, "mem_28398");
        memblock_unref(ctx, &mem_28395, "mem_28395");
        memblock_unref(ctx, &mem_28384, "mem_28384");
        memblock_unref(ctx, &mem_28381, "mem_28381");
        memblock_unref(ctx, &mem_28376, "mem_28376");
        memblock_unref(ctx, &mem_28371, "mem_28371");
    } else {
        cond_26400 = 0;
    }
    memblock_unref(ctx, &mem_28368, "mem_28368");
    
    bool cond_26471;
    
    if (cond_26400) {
        struct memblock mem_28422;
        
        mem_28422.references = NULL;
        memblock_alloc(ctx, &mem_28422, 16, "mem_28422");
        
        struct memblock mem_28427;
        
        mem_28427.references = NULL;
        memblock_alloc(ctx, &mem_28427, 8, "mem_28427");
        for (int32_t i_27518 = 0; i_27518 < 2; i_27518++) {
            for (int32_t i_29154 = 0; i_29154 < 2; i_29154++) {
                *(int32_t *) &mem_28427.mem[i_29154 * 4] = i_27518;
            }
            memmove(mem_28422.mem + 2 * i_27518 * 4, mem_28427.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_28427, "mem_28427");
        
        struct memblock mem_28432;
        
        mem_28432.references = NULL;
        memblock_alloc(ctx, &mem_28432, 16, "mem_28432");
        
        struct memblock mem_28435;
        
        mem_28435.references = NULL;
        memblock_alloc(ctx, &mem_28435, 16, "mem_28435");
        
        int32_t discard_27528;
        int32_t scanacc_27522 = 0;
        
        for (int32_t i_27525 = 0; i_27525 < 4; i_27525++) {
            bool not_arg_26482 = i_27525 == 0;
            bool res_26483 = !not_arg_26482;
            int32_t part_res_26484;
            
            if (res_26483) {
                part_res_26484 = 0;
            } else {
                part_res_26484 = 1;
            }
            
            int32_t part_res_26485;
            
            if (res_26483) {
                part_res_26485 = 1;
            } else {
                part_res_26485 = 0;
            }
            
            int32_t zz_26480 = part_res_26485 + scanacc_27522;
            
            *(int32_t *) &mem_28432.mem[i_27525 * 4] = zz_26480;
            *(int32_t *) &mem_28435.mem[i_27525 * 4] = part_res_26484;
            
            int32_t scanacc_tmp_29155 = zz_26480;
            
            scanacc_27522 = scanacc_tmp_29155;
        }
        discard_27528 = scanacc_27522;
        
        int32_t last_offset_26486 = *(int32_t *) &mem_28432.mem[12];
        int64_t binop_x_28445 = sext_i32_i64(last_offset_26486);
        int64_t bytes_28444 = 4 * binop_x_28445;
        struct memblock mem_28446;
        
        mem_28446.references = NULL;
        memblock_alloc(ctx, &mem_28446, bytes_28444, "mem_28446");
        
        struct memblock mem_28449;
        
        mem_28449.references = NULL;
        memblock_alloc(ctx, &mem_28449, bytes_28444, "mem_28449");
        
        struct memblock mem_28452;
        
        mem_28452.references = NULL;
        memblock_alloc(ctx, &mem_28452, bytes_28444, "mem_28452");
        for (int32_t write_iter_27529 = 0; write_iter_27529 < 4;
             write_iter_27529++) {
            int32_t write_iv_27533 =
                    *(int32_t *) &mem_28435.mem[write_iter_27529 * 4];
            int32_t write_iv_27534 =
                    *(int32_t *) &mem_28432.mem[write_iter_27529 * 4];
            int32_t new_index_28112 = squot32(write_iter_27529, 2);
            int32_t binop_y_28114 = 2 * new_index_28112;
            int32_t new_index_28115 = write_iter_27529 - binop_y_28114;
            int32_t write_iv_27535 =
                    *(int32_t *) &mem_28422.mem[(new_index_28112 * 2 +
                                                 new_index_28115) * 4];
            bool is_this_one_26498 = write_iv_27533 == 0;
            int32_t this_offset_26499 = -1 + write_iv_27534;
            int32_t total_res_26500;
            
            if (is_this_one_26498) {
                total_res_26500 = this_offset_26499;
            } else {
                total_res_26500 = -1;
            }
            
            bool less_than_zzero_27538 = slt32(total_res_26500, 0);
            bool greater_than_sizze_27539 = sle32(last_offset_26486,
                                                  total_res_26500);
            bool outside_bounds_dim_27540 = less_than_zzero_27538 ||
                 greater_than_sizze_27539;
            
            if (!outside_bounds_dim_27540) {
                *(int32_t *) &mem_28446.mem[total_res_26500 * 4] =
                    write_iv_27535;
            }
            if (!outside_bounds_dim_27540) {
                *(int32_t *) &mem_28449.mem[total_res_26500 * 4] =
                    new_index_28115;
            }
            if (!outside_bounds_dim_27540) {
                *(int32_t *) &mem_28452.mem[total_res_26500 * 4] =
                    write_iter_27529;
            }
        }
        memblock_unref(ctx, &mem_28422, "mem_28422");
        memblock_unref(ctx, &mem_28432, "mem_28432");
        memblock_unref(ctx, &mem_28435, "mem_28435");
        
        int32_t x_26501 = abs(last_offset_26486);
        bool empty_slice_26502 = x_26501 == 0;
        int32_t m_26503 = x_26501 - 1;
        bool zzero_leq_i_p_m_t_s_26504 = sle32(0, m_26503);
        bool i_p_m_t_s_leq_w_26505 = slt32(m_26503, last_offset_26486);
        bool y_26506 = zzero_leq_i_p_m_t_s_26504 && i_p_m_t_s_leq_w_26505;
        bool ok_or_empty_26507 = empty_slice_26502 || y_26506;
        bool index_certs_26508;
        
        if (!ok_or_empty_26507) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:157:32-157:36 -> tupleTest.fut:29:14-29:30 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_26486,
                                   "] out of bounds for array of shape [",
                                   last_offset_26486, "].");
            memblock_unref(ctx, &mem_28452, "mem_28452");
            memblock_unref(ctx, &mem_28449, "mem_28449");
            memblock_unref(ctx, &mem_28446, "mem_28446");
            memblock_unref(ctx, &mem_28435, "mem_28435");
            memblock_unref(ctx, &mem_28432, "mem_28432");
            memblock_unref(ctx, &mem_28427, "mem_28427");
            memblock_unref(ctx, &mem_28422, "mem_28422");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        struct memblock mem_28473;
        
        mem_28473.references = NULL;
        memblock_alloc(ctx, &mem_28473, 16, "mem_28473");
        for (int32_t i_29161 = 0; i_29161 < 4; i_29161++) {
            *(int32_t *) &mem_28473.mem[i_29161 * 4] = 0;
        }
        for (int32_t write_iter_27556 = 0; write_iter_27556 < x_26501;
             write_iter_27556++) {
            int32_t write_iv_27558 =
                    *(int32_t *) &mem_28446.mem[write_iter_27556 * 4];
            int32_t write_iv_27559 =
                    *(int32_t *) &mem_28449.mem[write_iter_27556 * 4];
            int32_t write_iv_27560 =
                    *(int32_t *) &mem_28452.mem[write_iter_27556 * 4];
            int32_t x_26517 = 2 * write_iv_27558;
            int32_t res_26518 = x_26517 + write_iv_27559;
            bool less_than_zzero_27561 = slt32(res_26518, 0);
            bool greater_than_sizze_27562 = sle32(4, res_26518);
            bool outside_bounds_dim_27563 = less_than_zzero_27561 ||
                 greater_than_sizze_27562;
            
            if (!outside_bounds_dim_27563) {
                *(int32_t *) &mem_28473.mem[res_26518 * 4] = write_iv_27560;
            }
        }
        memblock_unref(ctx, &mem_28446, "mem_28446");
        memblock_unref(ctx, &mem_28449, "mem_28449");
        memblock_unref(ctx, &mem_28452, "mem_28452");
        
        bool all_equal_26519;
        bool redout_27567 = 1;
        
        for (int32_t i_27568 = 0; i_27568 < 4; i_27568++) {
            int32_t y_26524 = *(int32_t *) &mem_28473.mem[i_27568 * 4];
            bool res_26525 = i_27568 == y_26524;
            bool res_26522 = res_26525 && redout_27567;
            bool redout_tmp_29163 = res_26522;
            
            redout_27567 = redout_tmp_29163;
        }
        all_equal_26519 = redout_27567;
        memblock_unref(ctx, &mem_28473, "mem_28473");
        cond_26471 = all_equal_26519;
        memblock_unref(ctx, &mem_28473, "mem_28473");
        memblock_unref(ctx, &mem_28452, "mem_28452");
        memblock_unref(ctx, &mem_28449, "mem_28449");
        memblock_unref(ctx, &mem_28446, "mem_28446");
        memblock_unref(ctx, &mem_28435, "mem_28435");
        memblock_unref(ctx, &mem_28432, "mem_28432");
        memblock_unref(ctx, &mem_28427, "mem_28427");
        memblock_unref(ctx, &mem_28422, "mem_28422");
    } else {
        cond_26471 = 0;
    }
    
    struct memblock mem_28482;
    
    mem_28482.references = NULL;
    memblock_alloc(ctx, &mem_28482, 0, "mem_28482");
    
    struct memblock mem_28485;
    
    mem_28485.references = NULL;
    memblock_alloc(ctx, &mem_28485, 16, "mem_28485");
    
    struct memblock mem_28490;
    
    mem_28490.references = NULL;
    memblock_alloc(ctx, &mem_28490, 8, "mem_28490");
    for (int32_t i_27584 = 0; i_27584 < 2; i_27584++) {
        for (int32_t i_29165 = 0; i_29165 < 2; i_29165++) {
            *(int32_t *) &mem_28490.mem[i_29165 * 4] = i_27584;
        }
        memmove(mem_28485.mem + 2 * i_27584 * 4, mem_28490.mem + 0, 2 *
                sizeof(int32_t));
    }
    memblock_unref(ctx, &mem_28490, "mem_28490");
    
    struct memblock mem_28495;
    
    mem_28495.references = NULL;
    memblock_alloc(ctx, &mem_28495, 16, "mem_28495");
    
    struct memblock mem_28498;
    
    mem_28498.references = NULL;
    memblock_alloc(ctx, &mem_28498, 16, "mem_28498");
    
    int32_t discard_27594;
    int32_t scanacc_27588 = 0;
    
    for (int32_t i_27591 = 0; i_27591 < 4; i_27591++) {
        bool not_arg_26554 = i_27591 == 0;
        bool res_26555 = !not_arg_26554;
        int32_t part_res_26556;
        
        if (res_26555) {
            part_res_26556 = 0;
        } else {
            part_res_26556 = 1;
        }
        
        int32_t part_res_26557;
        
        if (res_26555) {
            part_res_26557 = 1;
        } else {
            part_res_26557 = 0;
        }
        
        int32_t zz_26552 = part_res_26557 + scanacc_27588;
        
        *(int32_t *) &mem_28495.mem[i_27591 * 4] = zz_26552;
        *(int32_t *) &mem_28498.mem[i_27591 * 4] = part_res_26556;
        
        int32_t scanacc_tmp_29166 = zz_26552;
        
        scanacc_27588 = scanacc_tmp_29166;
    }
    discard_27594 = scanacc_27588;
    
    int32_t last_offset_26558 = *(int32_t *) &mem_28495.mem[12];
    int64_t binop_x_28508 = sext_i32_i64(last_offset_26558);
    int64_t bytes_28507 = 4 * binop_x_28508;
    struct memblock mem_28509;
    
    mem_28509.references = NULL;
    memblock_alloc(ctx, &mem_28509, bytes_28507, "mem_28509");
    
    struct memblock mem_28512;
    
    mem_28512.references = NULL;
    memblock_alloc(ctx, &mem_28512, bytes_28507, "mem_28512");
    
    struct memblock mem_28515;
    
    mem_28515.references = NULL;
    memblock_alloc(ctx, &mem_28515, bytes_28507, "mem_28515");
    for (int32_t write_iter_27595 = 0; write_iter_27595 < 4;
         write_iter_27595++) {
        int32_t write_iv_27599 = *(int32_t *) &mem_28498.mem[write_iter_27595 *
                                                             4];
        int32_t write_iv_27600 = *(int32_t *) &mem_28495.mem[write_iter_27595 *
                                                             4];
        int32_t new_index_28131 = squot32(write_iter_27595, 2);
        int32_t binop_y_28133 = 2 * new_index_28131;
        int32_t new_index_28134 = write_iter_27595 - binop_y_28133;
        int32_t write_iv_27601 = *(int32_t *) &mem_28485.mem[(new_index_28131 *
                                                              2 +
                                                              new_index_28134) *
                                                             4];
        bool is_this_one_26570 = write_iv_27599 == 0;
        int32_t this_offset_26571 = -1 + write_iv_27600;
        int32_t total_res_26572;
        
        if (is_this_one_26570) {
            total_res_26572 = this_offset_26571;
        } else {
            total_res_26572 = -1;
        }
        
        bool less_than_zzero_27604 = slt32(total_res_26572, 0);
        bool greater_than_sizze_27605 = sle32(last_offset_26558,
                                              total_res_26572);
        bool outside_bounds_dim_27606 = less_than_zzero_27604 ||
             greater_than_sizze_27605;
        
        if (!outside_bounds_dim_27606) {
            *(int32_t *) &mem_28509.mem[total_res_26572 * 4] = write_iv_27601;
        }
        if (!outside_bounds_dim_27606) {
            *(int32_t *) &mem_28512.mem[total_res_26572 * 4] = new_index_28134;
        }
        if (!outside_bounds_dim_27606) {
            *(int32_t *) &mem_28515.mem[total_res_26572 * 4] = write_iter_27595;
        }
    }
    memblock_unref(ctx, &mem_28485, "mem_28485");
    memblock_unref(ctx, &mem_28495, "mem_28495");
    memblock_unref(ctx, &mem_28498, "mem_28498");
    
    int32_t x_26573 = abs(last_offset_26558);
    bool empty_slice_26574 = x_26573 == 0;
    int32_t m_26575 = x_26573 - 1;
    bool zzero_leq_i_p_m_t_s_26576 = sle32(0, m_26575);
    bool i_p_m_t_s_leq_w_26577 = slt32(m_26575, last_offset_26558);
    bool y_26578 = zzero_leq_i_p_m_t_s_26576 && i_p_m_t_s_leq_w_26577;
    bool ok_or_empty_26579 = empty_slice_26574 || y_26578;
    bool index_certs_26580;
    
    if (!ok_or_empty_26579) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:158:12-158:16 -> tupleTest.fut:53:13-53:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", last_offset_26558,
                               "] out of bounds for array of shape [",
                               last_offset_26558, "].");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    struct memblock mem_28536;
    
    mem_28536.references = NULL;
    memblock_alloc(ctx, &mem_28536, 4, "mem_28536");
    for (int32_t i_29172 = 0; i_29172 < 1; i_29172++) {
        *(int32_t *) &mem_28536.mem[i_29172 * 4] = 1;
    }
    
    struct memblock mem_28539;
    
    mem_28539.references = NULL;
    memblock_alloc(ctx, &mem_28539, 4, "mem_28539");
    for (int32_t i_29173 = 0; i_29173 < 1; i_29173++) {
        *(int32_t *) &mem_28539.mem[i_29173 * 4] = 0;
    }
    
    struct memblock mem_28542;
    
    mem_28542.references = NULL;
    memblock_alloc(ctx, &mem_28542, 4, "mem_28542");
    for (int32_t i_29174 = 0; i_29174 < 1; i_29174++) {
        *(int32_t *) &mem_28542.mem[i_29174 * 4] = 4;
    }
    
    int32_t conc_tmp_26588 = 1 + x_26573;
    int32_t res_26589;
    int32_t redout_27622 = x_26573;
    
    for (int32_t i_27623 = 0; i_27623 < x_26573; i_27623++) {
        int32_t x_26593 = *(int32_t *) &mem_28509.mem[i_27623 * 4];
        int32_t x_26594 = *(int32_t *) &mem_28512.mem[i_27623 * 4];
        bool cond_26596 = x_26593 == 1;
        bool cond_26597 = x_26594 == 0;
        bool eq_26598 = cond_26596 && cond_26597;
        int32_t res_26599;
        
        if (eq_26598) {
            res_26599 = i_27623;
        } else {
            res_26599 = x_26573;
        }
        
        int32_t res_26592 = smin32(res_26599, redout_27622);
        int32_t redout_tmp_29175 = res_26592;
        
        redout_27622 = redout_tmp_29175;
    }
    res_26589 = redout_27622;
    
    bool cond_26600 = res_26589 == x_26573;
    int32_t res_26601;
    
    if (cond_26600) {
        res_26601 = -1;
    } else {
        res_26601 = res_26589;
    }
    
    bool eq_x_zz_26602 = -1 == res_26589;
    bool not_p_26603 = !cond_26600;
    bool p_and_eq_x_y_26604 = eq_x_zz_26602 && not_p_26603;
    bool cond_26605 = cond_26600 || p_and_eq_x_y_26604;
    bool cond_26606 = !cond_26605;
    int32_t sizze_26607;
    
    if (cond_26606) {
        sizze_26607 = x_26573;
    } else {
        sizze_26607 = conc_tmp_26588;
    }
    
    int64_t binop_x_28544 = sext_i32_i64(x_26573);
    int64_t bytes_28543 = 4 * binop_x_28544;
    int64_t binop_x_28549 = sext_i32_i64(sizze_26607);
    int64_t bytes_28548 = 4 * binop_x_28549;
    int64_t binop_x_28557 = sext_i32_i64(conc_tmp_26588);
    int64_t bytes_28556 = 4 * binop_x_28557;
    int64_t res_mem_sizze_28565;
    struct memblock res_mem_28566;
    
    res_mem_28566.references = NULL;
    
    int64_t res_mem_sizze_28567;
    struct memblock res_mem_28568;
    
    res_mem_28568.references = NULL;
    
    int64_t res_mem_sizze_28569;
    struct memblock res_mem_28570;
    
    res_mem_28570.references = NULL;
    if (cond_26606) {
        struct memblock mem_28545;
        
        mem_28545.references = NULL;
        memblock_alloc(ctx, &mem_28545, bytes_28543, "mem_28545");
        memmove(mem_28545.mem + 0, mem_28515.mem + 0, x_26573 *
                sizeof(int32_t));
        
        bool less_than_zzero_27626 = slt32(res_26601, 0);
        bool greater_than_sizze_27627 = sle32(x_26573, res_26601);
        bool outside_bounds_dim_27628 = less_than_zzero_27626 ||
             greater_than_sizze_27627;
        
        if (!outside_bounds_dim_27628) {
            *(int32_t *) &mem_28545.mem[res_26601 * 4] = 4;
        }
        
        struct memblock mem_28550;
        
        mem_28550.references = NULL;
        memblock_alloc(ctx, &mem_28550, bytes_28548, "mem_28550");
        memmove(mem_28550.mem + 0, mem_28509.mem + 0, sizze_26607 *
                sizeof(int32_t));
        
        struct memblock mem_28554;
        
        mem_28554.references = NULL;
        memblock_alloc(ctx, &mem_28554, bytes_28548, "mem_28554");
        memmove(mem_28554.mem + 0, mem_28512.mem + 0, sizze_26607 *
                sizeof(int32_t));
        res_mem_sizze_28565 = bytes_28548;
        memblock_set(ctx, &res_mem_28566, &mem_28550, "mem_28550");
        res_mem_sizze_28567 = bytes_28548;
        memblock_set(ctx, &res_mem_28568, &mem_28554, "mem_28554");
        res_mem_sizze_28569 = bytes_28543;
        memblock_set(ctx, &res_mem_28570, &mem_28545, "mem_28545");
        memblock_unref(ctx, &mem_28554, "mem_28554");
        memblock_unref(ctx, &mem_28550, "mem_28550");
        memblock_unref(ctx, &mem_28545, "mem_28545");
    } else {
        struct memblock mem_28558;
        
        mem_28558.references = NULL;
        memblock_alloc(ctx, &mem_28558, bytes_28556, "mem_28558");
        
        int32_t tmp_offs_29176 = 0;
        
        memmove(mem_28558.mem + tmp_offs_29176 * 4, mem_28509.mem + 0, x_26573 *
                sizeof(int32_t));
        tmp_offs_29176 += x_26573;
        memmove(mem_28558.mem + tmp_offs_29176 * 4, mem_28536.mem + 0,
                sizeof(int32_t));
        tmp_offs_29176 += 1;
        
        struct memblock mem_28561;
        
        mem_28561.references = NULL;
        memblock_alloc(ctx, &mem_28561, bytes_28556, "mem_28561");
        
        int32_t tmp_offs_29177 = 0;
        
        memmove(mem_28561.mem + tmp_offs_29177 * 4, mem_28512.mem + 0, x_26573 *
                sizeof(int32_t));
        tmp_offs_29177 += x_26573;
        memmove(mem_28561.mem + tmp_offs_29177 * 4, mem_28539.mem + 0,
                sizeof(int32_t));
        tmp_offs_29177 += 1;
        
        struct memblock mem_28564;
        
        mem_28564.references = NULL;
        memblock_alloc(ctx, &mem_28564, bytes_28556, "mem_28564");
        
        int32_t tmp_offs_29178 = 0;
        
        memmove(mem_28564.mem + tmp_offs_29178 * 4, mem_28515.mem + 0, x_26573 *
                sizeof(int32_t));
        tmp_offs_29178 += x_26573;
        memmove(mem_28564.mem + tmp_offs_29178 * 4, mem_28542.mem + 0,
                sizeof(int32_t));
        tmp_offs_29178 += 1;
        res_mem_sizze_28565 = bytes_28556;
        memblock_set(ctx, &res_mem_28566, &mem_28558, "mem_28558");
        res_mem_sizze_28567 = bytes_28556;
        memblock_set(ctx, &res_mem_28568, &mem_28561, "mem_28561");
        res_mem_sizze_28569 = bytes_28556;
        memblock_set(ctx, &res_mem_28570, &mem_28564, "mem_28564");
        memblock_unref(ctx, &mem_28564, "mem_28564");
        memblock_unref(ctx, &mem_28561, "mem_28561");
        memblock_unref(ctx, &mem_28558, "mem_28558");
    }
    memblock_unref(ctx, &mem_28509, "mem_28509");
    memblock_unref(ctx, &mem_28512, "mem_28512");
    memblock_unref(ctx, &mem_28515, "mem_28515");
    memblock_unref(ctx, &mem_28536, "mem_28536");
    
    bool eq_x_y_26622 = 0 == x_26573;
    bool p_and_eq_x_y_26623 = cond_26606 && eq_x_y_26622;
    bool both_empty_26624 = p_and_eq_x_y_26623 && p_and_eq_x_y_26623;
    bool p_and_eq_x_y_26625 = cond_26606 && cond_26606;
    bool p_and_eq_x_y_26626 = cond_26605 && cond_26605;
    bool dim_match_26627 = p_and_eq_x_y_26625 || p_and_eq_x_y_26626;
    bool empty_or_match_26628 = both_empty_26624 || dim_match_26627;
    bool empty_or_match_cert_26629;
    
    if (!empty_or_match_26628) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:158:12-158:16 -> tupleTest.fut:54:13-54:34 -> tupleSparse.fut:154:1-161:83",
                               "Function return value does not match shape of type ",
                               "matrix", " ", 2, " ", 2);
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    struct memblock mem_28573;
    
    mem_28573.references = NULL;
    memblock_alloc(ctx, &mem_28573, 12, "mem_28573");
    
    struct memblock static_array_29179 = ctx->static_array_29179;
    
    memmove(mem_28573.mem + 0, static_array_29179.mem + 0, 3 * sizeof(int32_t));
    
    bool eq_x_y_26631 = 3 == x_26573;
    bool eq_x_zz_26632 = 3 == conc_tmp_26588;
    bool p_and_eq_x_y_26633 = cond_26606 && eq_x_y_26631;
    bool p_and_eq_x_y_26634 = cond_26605 && eq_x_zz_26632;
    bool dim_eq_26635 = p_and_eq_x_y_26633 || p_and_eq_x_y_26634;
    bool arrays_equal_26636;
    
    if (dim_eq_26635) {
        bool all_equal_26638;
        bool redout_27632 = 1;
        
        for (int32_t i_27633 = 0; i_27633 < sizze_26607; i_27633++) {
            int32_t x_26642 = *(int32_t *) &res_mem_28570.mem[i_27633 * 4];
            int32_t y_26643 = *(int32_t *) &mem_28573.mem[i_27633 * 4];
            bool res_26644 = x_26642 == y_26643;
            bool res_26641 = res_26644 && redout_27632;
            bool redout_tmp_29180 = res_26641;
            
            redout_27632 = redout_tmp_29180;
        }
        all_equal_26638 = redout_27632;
        arrays_equal_26636 = all_equal_26638;
    } else {
        arrays_equal_26636 = 0;
    }
    memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
    memblock_unref(ctx, &mem_28573, "mem_28573");
    
    bool res_26645;
    
    if (arrays_equal_26636) {
        bool arrays_equal_26646;
        
        if (dim_eq_26635) {
            bool all_equal_26648;
            bool redout_27634 = 1;
            
            for (int32_t i_27635 = 0; i_27635 < sizze_26607; i_27635++) {
                int32_t x_26652 = *(int32_t *) &res_mem_28566.mem[i_27635 * 4];
                int32_t y_26653 = *(int32_t *) &mem_28362.mem[i_27635 * 4];
                bool res_26654 = x_26652 == y_26653;
                bool res_26651 = res_26654 && redout_27634;
                bool redout_tmp_29181 = res_26651;
                
                redout_27634 = redout_tmp_29181;
            }
            all_equal_26648 = redout_27634;
            arrays_equal_26646 = all_equal_26648;
        } else {
            arrays_equal_26646 = 0;
        }
        
        bool arrays_equal_26655;
        
        if (dim_eq_26635) {
            bool all_equal_26657;
            bool redout_27636 = 1;
            
            for (int32_t i_27637 = 0; i_27637 < sizze_26607; i_27637++) {
                int32_t x_26661 = *(int32_t *) &res_mem_28568.mem[i_27637 * 4];
                int32_t y_26662 = *(int32_t *) &mem_28365.mem[i_27637 * 4];
                bool res_26663 = x_26661 == y_26662;
                bool res_26660 = res_26663 && redout_27636;
                bool redout_tmp_29182 = res_26660;
                
                redout_27636 = redout_tmp_29182;
            }
            all_equal_26657 = redout_27636;
            arrays_equal_26655 = all_equal_26657;
        } else {
            arrays_equal_26655 = 0;
        }
        
        bool eq_26664 = arrays_equal_26646 && arrays_equal_26655;
        
        res_26645 = eq_26664;
    } else {
        res_26645 = 0;
    }
    memblock_unref(ctx, &mem_28362, "mem_28362");
    memblock_unref(ctx, &mem_28365, "mem_28365");
    memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
    memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
    
    struct memblock mem_28576;
    
    mem_28576.references = NULL;
    memblock_alloc(ctx, &mem_28576, 16, "mem_28576");
    
    struct memblock static_array_29183 = ctx->static_array_29183;
    
    memmove(mem_28576.mem + 0, static_array_29183.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_28579;
    
    mem_28579.references = NULL;
    memblock_alloc(ctx, &mem_28579, 16, "mem_28579");
    
    struct memblock static_array_29184 = ctx->static_array_29184;
    
    memmove(mem_28579.mem + 0, static_array_29184.mem + 0, 4 * sizeof(int32_t));
    
    bool cond_26667;
    
    if (res_26645) {
        struct memblock mem_28582;
        
        mem_28582.references = NULL;
        memblock_alloc(ctx, &mem_28582, 16, "mem_28582");
        
        struct memblock mem_28587;
        
        mem_28587.references = NULL;
        memblock_alloc(ctx, &mem_28587, 8, "mem_28587");
        for (int32_t i_27640 = 0; i_27640 < 2; i_27640++) {
            for (int32_t i_29186 = 0; i_29186 < 2; i_29186++) {
                *(int32_t *) &mem_28587.mem[i_29186 * 4] = i_27640;
            }
            memmove(mem_28582.mem + 2 * i_27640 * 4, mem_28587.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_28587, "mem_28587");
        
        struct memblock mem_28592;
        
        mem_28592.references = NULL;
        memblock_alloc(ctx, &mem_28592, 16, "mem_28592");
        
        struct memblock mem_28595;
        
        mem_28595.references = NULL;
        memblock_alloc(ctx, &mem_28595, 16, "mem_28595");
        
        int32_t discard_27650;
        int32_t scanacc_27644 = 0;
        
        for (int32_t i_27647 = 0; i_27647 < 4; i_27647++) {
            bool not_arg_26678 = i_27647 == 0;
            bool res_26679 = !not_arg_26678;
            int32_t part_res_26680;
            
            if (res_26679) {
                part_res_26680 = 0;
            } else {
                part_res_26680 = 1;
            }
            
            int32_t part_res_26681;
            
            if (res_26679) {
                part_res_26681 = 1;
            } else {
                part_res_26681 = 0;
            }
            
            int32_t zz_26676 = part_res_26681 + scanacc_27644;
            
            *(int32_t *) &mem_28592.mem[i_27647 * 4] = zz_26676;
            *(int32_t *) &mem_28595.mem[i_27647 * 4] = part_res_26680;
            
            int32_t scanacc_tmp_29187 = zz_26676;
            
            scanacc_27644 = scanacc_tmp_29187;
        }
        discard_27650 = scanacc_27644;
        
        int32_t last_offset_26682 = *(int32_t *) &mem_28592.mem[12];
        int64_t binop_x_28605 = sext_i32_i64(last_offset_26682);
        int64_t bytes_28604 = 4 * binop_x_28605;
        struct memblock mem_28606;
        
        mem_28606.references = NULL;
        memblock_alloc(ctx, &mem_28606, bytes_28604, "mem_28606");
        
        struct memblock mem_28609;
        
        mem_28609.references = NULL;
        memblock_alloc(ctx, &mem_28609, bytes_28604, "mem_28609");
        
        struct memblock mem_28612;
        
        mem_28612.references = NULL;
        memblock_alloc(ctx, &mem_28612, bytes_28604, "mem_28612");
        for (int32_t write_iter_27651 = 0; write_iter_27651 < 4;
             write_iter_27651++) {
            int32_t write_iv_27655 =
                    *(int32_t *) &mem_28595.mem[write_iter_27651 * 4];
            int32_t write_iv_27656 =
                    *(int32_t *) &mem_28592.mem[write_iter_27651 * 4];
            int32_t new_index_28148 = squot32(write_iter_27651, 2);
            int32_t binop_y_28150 = 2 * new_index_28148;
            int32_t new_index_28151 = write_iter_27651 - binop_y_28150;
            int32_t write_iv_27657 =
                    *(int32_t *) &mem_28582.mem[(new_index_28148 * 2 +
                                                 new_index_28151) * 4];
            bool is_this_one_26694 = write_iv_27655 == 0;
            int32_t this_offset_26695 = -1 + write_iv_27656;
            int32_t total_res_26696;
            
            if (is_this_one_26694) {
                total_res_26696 = this_offset_26695;
            } else {
                total_res_26696 = -1;
            }
            
            bool less_than_zzero_27660 = slt32(total_res_26696, 0);
            bool greater_than_sizze_27661 = sle32(last_offset_26682,
                                                  total_res_26696);
            bool outside_bounds_dim_27662 = less_than_zzero_27660 ||
                 greater_than_sizze_27661;
            
            if (!outside_bounds_dim_27662) {
                *(int32_t *) &mem_28606.mem[total_res_26696 * 4] =
                    write_iv_27657;
            }
            if (!outside_bounds_dim_27662) {
                *(int32_t *) &mem_28609.mem[total_res_26696 * 4] =
                    new_index_28151;
            }
            if (!outside_bounds_dim_27662) {
                *(int32_t *) &mem_28612.mem[total_res_26696 * 4] =
                    write_iter_27651;
            }
        }
        memblock_unref(ctx, &mem_28582, "mem_28582");
        memblock_unref(ctx, &mem_28592, "mem_28592");
        memblock_unref(ctx, &mem_28595, "mem_28595");
        
        int32_t x_26697 = abs(last_offset_26682);
        bool empty_slice_26698 = x_26697 == 0;
        int32_t m_26699 = x_26697 - 1;
        bool zzero_leq_i_p_m_t_s_26700 = sle32(0, m_26699);
        bool i_p_m_t_s_leq_w_26701 = slt32(m_26699, last_offset_26682);
        bool y_26702 = zzero_leq_i_p_m_t_s_26700 && i_p_m_t_s_leq_w_26701;
        bool ok_or_empty_26703 = empty_slice_26698 || y_26702;
        bool index_certs_26704;
        
        if (!ok_or_empty_26703) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:158:21-158:25 -> tupleTest.fut:60:13-60:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_26682,
                                   "] out of bounds for array of shape [",
                                   last_offset_26682, "].");
            memblock_unref(ctx, &mem_28612, "mem_28612");
            memblock_unref(ctx, &mem_28609, "mem_28609");
            memblock_unref(ctx, &mem_28606, "mem_28606");
            memblock_unref(ctx, &mem_28595, "mem_28595");
            memblock_unref(ctx, &mem_28592, "mem_28592");
            memblock_unref(ctx, &mem_28587, "mem_28587");
            memblock_unref(ctx, &mem_28582, "mem_28582");
            memblock_unref(ctx, &mem_28579, "mem_28579");
            memblock_unref(ctx, &mem_28576, "mem_28576");
            memblock_unref(ctx, &mem_28573, "mem_28573");
            memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
            memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
            memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
            memblock_unref(ctx, &mem_28542, "mem_28542");
            memblock_unref(ctx, &mem_28539, "mem_28539");
            memblock_unref(ctx, &mem_28536, "mem_28536");
            memblock_unref(ctx, &mem_28515, "mem_28515");
            memblock_unref(ctx, &mem_28512, "mem_28512");
            memblock_unref(ctx, &mem_28509, "mem_28509");
            memblock_unref(ctx, &mem_28498, "mem_28498");
            memblock_unref(ctx, &mem_28495, "mem_28495");
            memblock_unref(ctx, &mem_28490, "mem_28490");
            memblock_unref(ctx, &mem_28485, "mem_28485");
            memblock_unref(ctx, &mem_28482, "mem_28482");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        int32_t conc_tmp_26709 = 1 + x_26697;
        int32_t res_26710;
        int32_t redout_27678 = x_26697;
        
        for (int32_t i_27679 = 0; i_27679 < x_26697; i_27679++) {
            int32_t x_26714 = *(int32_t *) &mem_28606.mem[i_27679 * 4];
            int32_t x_26715 = *(int32_t *) &mem_28609.mem[i_27679 * 4];
            bool cond_26717 = x_26714 == 0;
            bool cond_26718 = x_26715 == 0;
            bool eq_26719 = cond_26717 && cond_26718;
            int32_t res_26720;
            
            if (eq_26719) {
                res_26720 = i_27679;
            } else {
                res_26720 = x_26697;
            }
            
            int32_t res_26713 = smin32(res_26720, redout_27678);
            int32_t redout_tmp_29193 = res_26713;
            
            redout_27678 = redout_tmp_29193;
        }
        res_26710 = redout_27678;
        
        bool cond_26721 = res_26710 == x_26697;
        int32_t res_26722;
        
        if (cond_26721) {
            res_26722 = -1;
        } else {
            res_26722 = res_26710;
        }
        
        bool eq_x_zz_26723 = -1 == res_26710;
        bool not_p_26724 = !cond_26721;
        bool p_and_eq_x_y_26725 = eq_x_zz_26723 && not_p_26724;
        bool cond_26726 = cond_26721 || p_and_eq_x_y_26725;
        bool cond_26727 = !cond_26726;
        int32_t sizze_26728;
        
        if (cond_26727) {
            sizze_26728 = x_26697;
        } else {
            sizze_26728 = conc_tmp_26709;
        }
        
        int64_t binop_x_28632 = sext_i32_i64(x_26697);
        int64_t bytes_28631 = 4 * binop_x_28632;
        int64_t binop_x_28637 = sext_i32_i64(sizze_26728);
        int64_t bytes_28636 = 4 * binop_x_28637;
        int64_t binop_x_28645 = sext_i32_i64(conc_tmp_26709);
        int64_t bytes_28644 = 4 * binop_x_28645;
        int64_t res_mem_sizze_28653;
        struct memblock res_mem_28654;
        
        res_mem_28654.references = NULL;
        
        int64_t res_mem_sizze_28655;
        struct memblock res_mem_28656;
        
        res_mem_28656.references = NULL;
        
        int64_t res_mem_sizze_28657;
        struct memblock res_mem_28658;
        
        res_mem_28658.references = NULL;
        if (cond_26727) {
            struct memblock mem_28633;
            
            mem_28633.references = NULL;
            memblock_alloc(ctx, &mem_28633, bytes_28631, "mem_28633");
            memmove(mem_28633.mem + 0, mem_28612.mem + 0, x_26697 *
                    sizeof(int32_t));
            
            bool less_than_zzero_27682 = slt32(res_26722, 0);
            bool greater_than_sizze_27683 = sle32(x_26697, res_26722);
            bool outside_bounds_dim_27684 = less_than_zzero_27682 ||
                 greater_than_sizze_27683;
            
            if (!outside_bounds_dim_27684) {
                *(int32_t *) &mem_28633.mem[res_26722 * 4] = 4;
            }
            
            struct memblock mem_28638;
            
            mem_28638.references = NULL;
            memblock_alloc(ctx, &mem_28638, bytes_28636, "mem_28638");
            memmove(mem_28638.mem + 0, mem_28606.mem + 0, sizze_26728 *
                    sizeof(int32_t));
            
            struct memblock mem_28642;
            
            mem_28642.references = NULL;
            memblock_alloc(ctx, &mem_28642, bytes_28636, "mem_28642");
            memmove(mem_28642.mem + 0, mem_28609.mem + 0, sizze_26728 *
                    sizeof(int32_t));
            res_mem_sizze_28653 = bytes_28636;
            memblock_set(ctx, &res_mem_28654, &mem_28638, "mem_28638");
            res_mem_sizze_28655 = bytes_28636;
            memblock_set(ctx, &res_mem_28656, &mem_28642, "mem_28642");
            res_mem_sizze_28657 = bytes_28631;
            memblock_set(ctx, &res_mem_28658, &mem_28633, "mem_28633");
            memblock_unref(ctx, &mem_28642, "mem_28642");
            memblock_unref(ctx, &mem_28638, "mem_28638");
            memblock_unref(ctx, &mem_28633, "mem_28633");
        } else {
            struct memblock mem_28646;
            
            mem_28646.references = NULL;
            memblock_alloc(ctx, &mem_28646, bytes_28644, "mem_28646");
            
            int32_t tmp_offs_29194 = 0;
            
            memmove(mem_28646.mem + tmp_offs_29194 * 4, mem_28606.mem + 0,
                    x_26697 * sizeof(int32_t));
            tmp_offs_29194 += x_26697;
            memmove(mem_28646.mem + tmp_offs_29194 * 4, mem_28539.mem + 0,
                    sizeof(int32_t));
            tmp_offs_29194 += 1;
            
            struct memblock mem_28649;
            
            mem_28649.references = NULL;
            memblock_alloc(ctx, &mem_28649, bytes_28644, "mem_28649");
            
            int32_t tmp_offs_29195 = 0;
            
            memmove(mem_28649.mem + tmp_offs_29195 * 4, mem_28609.mem + 0,
                    x_26697 * sizeof(int32_t));
            tmp_offs_29195 += x_26697;
            memmove(mem_28649.mem + tmp_offs_29195 * 4, mem_28539.mem + 0,
                    sizeof(int32_t));
            tmp_offs_29195 += 1;
            
            struct memblock mem_28652;
            
            mem_28652.references = NULL;
            memblock_alloc(ctx, &mem_28652, bytes_28644, "mem_28652");
            
            int32_t tmp_offs_29196 = 0;
            
            memmove(mem_28652.mem + tmp_offs_29196 * 4, mem_28612.mem + 0,
                    x_26697 * sizeof(int32_t));
            tmp_offs_29196 += x_26697;
            memmove(mem_28652.mem + tmp_offs_29196 * 4, mem_28542.mem + 0,
                    sizeof(int32_t));
            tmp_offs_29196 += 1;
            res_mem_sizze_28653 = bytes_28644;
            memblock_set(ctx, &res_mem_28654, &mem_28646, "mem_28646");
            res_mem_sizze_28655 = bytes_28644;
            memblock_set(ctx, &res_mem_28656, &mem_28649, "mem_28649");
            res_mem_sizze_28657 = bytes_28644;
            memblock_set(ctx, &res_mem_28658, &mem_28652, "mem_28652");
            memblock_unref(ctx, &mem_28652, "mem_28652");
            memblock_unref(ctx, &mem_28649, "mem_28649");
            memblock_unref(ctx, &mem_28646, "mem_28646");
        }
        memblock_unref(ctx, &mem_28606, "mem_28606");
        memblock_unref(ctx, &mem_28609, "mem_28609");
        memblock_unref(ctx, &mem_28612, "mem_28612");
        
        bool eq_x_y_26743 = 0 == x_26697;
        bool p_and_eq_x_y_26744 = cond_26727 && eq_x_y_26743;
        bool both_empty_26745 = p_and_eq_x_y_26744 && p_and_eq_x_y_26744;
        bool p_and_eq_x_y_26746 = cond_26727 && cond_26727;
        bool p_and_eq_x_y_26747 = cond_26726 && cond_26726;
        bool dim_match_26748 = p_and_eq_x_y_26746 || p_and_eq_x_y_26747;
        bool empty_or_match_26749 = both_empty_26745 || dim_match_26748;
        bool empty_or_match_cert_26750;
        
        if (!empty_or_match_26749) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:158:21-158:25 -> tupleTest.fut:61:13-61:34 -> tupleSparse.fut:154:1-161:83",
                                   "Function return value does not match shape of type ",
                                   "matrix", " ", 2, " ", 2);
            memblock_unref(ctx, &res_mem_28658, "res_mem_28658");
            memblock_unref(ctx, &res_mem_28656, "res_mem_28656");
            memblock_unref(ctx, &res_mem_28654, "res_mem_28654");
            memblock_unref(ctx, &mem_28612, "mem_28612");
            memblock_unref(ctx, &mem_28609, "mem_28609");
            memblock_unref(ctx, &mem_28606, "mem_28606");
            memblock_unref(ctx, &mem_28595, "mem_28595");
            memblock_unref(ctx, &mem_28592, "mem_28592");
            memblock_unref(ctx, &mem_28587, "mem_28587");
            memblock_unref(ctx, &mem_28582, "mem_28582");
            memblock_unref(ctx, &mem_28579, "mem_28579");
            memblock_unref(ctx, &mem_28576, "mem_28576");
            memblock_unref(ctx, &mem_28573, "mem_28573");
            memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
            memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
            memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
            memblock_unref(ctx, &mem_28542, "mem_28542");
            memblock_unref(ctx, &mem_28539, "mem_28539");
            memblock_unref(ctx, &mem_28536, "mem_28536");
            memblock_unref(ctx, &mem_28515, "mem_28515");
            memblock_unref(ctx, &mem_28512, "mem_28512");
            memblock_unref(ctx, &mem_28509, "mem_28509");
            memblock_unref(ctx, &mem_28498, "mem_28498");
            memblock_unref(ctx, &mem_28495, "mem_28495");
            memblock_unref(ctx, &mem_28490, "mem_28490");
            memblock_unref(ctx, &mem_28485, "mem_28485");
            memblock_unref(ctx, &mem_28482, "mem_28482");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        bool eq_x_y_26751 = 4 == x_26697;
        bool eq_x_zz_26752 = 4 == conc_tmp_26709;
        bool p_and_eq_x_y_26753 = cond_26727 && eq_x_y_26751;
        bool p_and_eq_x_y_26754 = cond_26726 && eq_x_zz_26752;
        bool dim_eq_26755 = p_and_eq_x_y_26753 || p_and_eq_x_y_26754;
        bool arrays_equal_26756;
        
        if (dim_eq_26755) {
            bool all_equal_26758;
            bool redout_27688 = 1;
            
            for (int32_t i_27689 = 0; i_27689 < sizze_26728; i_27689++) {
                int32_t x_26762 = *(int32_t *) &res_mem_28658.mem[i_27689 * 4];
                int32_t y_26763 = *(int32_t *) &mem_28309.mem[i_27689 * 4];
                bool res_26764 = x_26762 == y_26763;
                bool res_26761 = res_26764 && redout_27688;
                bool redout_tmp_29197 = res_26761;
                
                redout_27688 = redout_tmp_29197;
            }
            all_equal_26758 = redout_27688;
            arrays_equal_26756 = all_equal_26758;
        } else {
            arrays_equal_26756 = 0;
        }
        memblock_unref(ctx, &res_mem_28658, "res_mem_28658");
        
        bool res_26765;
        
        if (arrays_equal_26756) {
            bool arrays_equal_26766;
            
            if (dim_eq_26755) {
                bool all_equal_26768;
                bool redout_27690 = 1;
                
                for (int32_t i_27691 = 0; i_27691 < sizze_26728; i_27691++) {
                    int32_t x_26772 = *(int32_t *) &res_mem_28654.mem[i_27691 *
                                                                      4];
                    int32_t y_26773 = *(int32_t *) &mem_28576.mem[i_27691 * 4];
                    bool res_26774 = x_26772 == y_26773;
                    bool res_26771 = res_26774 && redout_27690;
                    bool redout_tmp_29198 = res_26771;
                    
                    redout_27690 = redout_tmp_29198;
                }
                all_equal_26768 = redout_27690;
                arrays_equal_26766 = all_equal_26768;
            } else {
                arrays_equal_26766 = 0;
            }
            
            bool arrays_equal_26775;
            
            if (dim_eq_26755) {
                bool all_equal_26777;
                bool redout_27692 = 1;
                
                for (int32_t i_27693 = 0; i_27693 < sizze_26728; i_27693++) {
                    int32_t x_26781 = *(int32_t *) &res_mem_28656.mem[i_27693 *
                                                                      4];
                    int32_t y_26782 = *(int32_t *) &mem_28579.mem[i_27693 * 4];
                    bool res_26783 = x_26781 == y_26782;
                    bool res_26780 = res_26783 && redout_27692;
                    bool redout_tmp_29199 = res_26780;
                    
                    redout_27692 = redout_tmp_29199;
                }
                all_equal_26777 = redout_27692;
                arrays_equal_26775 = all_equal_26777;
            } else {
                arrays_equal_26775 = 0;
            }
            
            bool eq_26784 = arrays_equal_26766 && arrays_equal_26775;
            
            res_26765 = eq_26784;
        } else {
            res_26765 = 0;
        }
        memblock_unref(ctx, &res_mem_28654, "res_mem_28654");
        memblock_unref(ctx, &res_mem_28656, "res_mem_28656");
        cond_26667 = res_26765;
        memblock_unref(ctx, &res_mem_28658, "res_mem_28658");
        memblock_unref(ctx, &res_mem_28656, "res_mem_28656");
        memblock_unref(ctx, &res_mem_28654, "res_mem_28654");
        memblock_unref(ctx, &mem_28612, "mem_28612");
        memblock_unref(ctx, &mem_28609, "mem_28609");
        memblock_unref(ctx, &mem_28606, "mem_28606");
        memblock_unref(ctx, &mem_28595, "mem_28595");
        memblock_unref(ctx, &mem_28592, "mem_28592");
        memblock_unref(ctx, &mem_28587, "mem_28587");
        memblock_unref(ctx, &mem_28582, "mem_28582");
    } else {
        cond_26667 = 0;
    }
    memblock_unref(ctx, &mem_28309, "mem_28309");
    memblock_unref(ctx, &mem_28539, "mem_28539");
    memblock_unref(ctx, &mem_28542, "mem_28542");
    memblock_unref(ctx, &mem_28576, "mem_28576");
    memblock_unref(ctx, &mem_28579, "mem_28579");
    
    bool res_26785;
    
    if (cond_26667) {
        struct memblock mem_28661;
        
        mem_28661.references = NULL;
        memblock_alloc(ctx, &mem_28661, 16, "mem_28661");
        
        struct memblock mem_28666;
        
        mem_28666.references = NULL;
        memblock_alloc(ctx, &mem_28666, 8, "mem_28666");
        for (int32_t i_27696 = 0; i_27696 < 2; i_27696++) {
            for (int32_t i_29201 = 0; i_29201 < 2; i_29201++) {
                *(int32_t *) &mem_28666.mem[i_29201 * 4] = i_27696;
            }
            memmove(mem_28661.mem + 2 * i_27696 * 4, mem_28666.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_28666, "mem_28666");
        
        struct memblock mem_28671;
        
        mem_28671.references = NULL;
        memblock_alloc(ctx, &mem_28671, 16, "mem_28671");
        
        struct memblock mem_28674;
        
        mem_28674.references = NULL;
        memblock_alloc(ctx, &mem_28674, 16, "mem_28674");
        
        int32_t discard_27706;
        int32_t scanacc_27700 = 0;
        
        for (int32_t i_27703 = 0; i_27703 < 4; i_27703++) {
            bool not_arg_26796 = i_27703 == 0;
            bool res_26797 = !not_arg_26796;
            int32_t part_res_26798;
            
            if (res_26797) {
                part_res_26798 = 0;
            } else {
                part_res_26798 = 1;
            }
            
            int32_t part_res_26799;
            
            if (res_26797) {
                part_res_26799 = 1;
            } else {
                part_res_26799 = 0;
            }
            
            int32_t zz_26794 = part_res_26799 + scanacc_27700;
            
            *(int32_t *) &mem_28671.mem[i_27703 * 4] = zz_26794;
            *(int32_t *) &mem_28674.mem[i_27703 * 4] = part_res_26798;
            
            int32_t scanacc_tmp_29202 = zz_26794;
            
            scanacc_27700 = scanacc_tmp_29202;
        }
        discard_27706 = scanacc_27700;
        
        int32_t last_offset_26800 = *(int32_t *) &mem_28671.mem[12];
        int64_t binop_x_28684 = sext_i32_i64(last_offset_26800);
        int64_t bytes_28683 = 4 * binop_x_28684;
        struct memblock mem_28685;
        
        mem_28685.references = NULL;
        memblock_alloc(ctx, &mem_28685, bytes_28683, "mem_28685");
        
        struct memblock mem_28688;
        
        mem_28688.references = NULL;
        memblock_alloc(ctx, &mem_28688, bytes_28683, "mem_28688");
        
        struct memblock mem_28691;
        
        mem_28691.references = NULL;
        memblock_alloc(ctx, &mem_28691, bytes_28683, "mem_28691");
        for (int32_t write_iter_27707 = 0; write_iter_27707 < 4;
             write_iter_27707++) {
            int32_t write_iv_27711 =
                    *(int32_t *) &mem_28674.mem[write_iter_27707 * 4];
            int32_t write_iv_27712 =
                    *(int32_t *) &mem_28671.mem[write_iter_27707 * 4];
            int32_t new_index_28165 = squot32(write_iter_27707, 2);
            int32_t binop_y_28167 = 2 * new_index_28165;
            int32_t new_index_28168 = write_iter_27707 - binop_y_28167;
            int32_t write_iv_27713 =
                    *(int32_t *) &mem_28661.mem[(new_index_28165 * 2 +
                                                 new_index_28168) * 4];
            bool is_this_one_26812 = write_iv_27711 == 0;
            int32_t this_offset_26813 = -1 + write_iv_27712;
            int32_t total_res_26814;
            
            if (is_this_one_26812) {
                total_res_26814 = this_offset_26813;
            } else {
                total_res_26814 = -1;
            }
            
            bool less_than_zzero_27716 = slt32(total_res_26814, 0);
            bool greater_than_sizze_27717 = sle32(last_offset_26800,
                                                  total_res_26814);
            bool outside_bounds_dim_27718 = less_than_zzero_27716 ||
                 greater_than_sizze_27717;
            
            if (!outside_bounds_dim_27718) {
                *(int32_t *) &mem_28685.mem[total_res_26814 * 4] =
                    write_iv_27713;
            }
            if (!outside_bounds_dim_27718) {
                *(int32_t *) &mem_28688.mem[total_res_26814 * 4] =
                    new_index_28168;
            }
            if (!outside_bounds_dim_27718) {
                *(int32_t *) &mem_28691.mem[total_res_26814 * 4] =
                    write_iter_27707;
            }
        }
        memblock_unref(ctx, &mem_28661, "mem_28661");
        memblock_unref(ctx, &mem_28671, "mem_28671");
        memblock_unref(ctx, &mem_28674, "mem_28674");
        
        int32_t x_26815 = abs(last_offset_26800);
        bool empty_slice_26816 = x_26815 == 0;
        int32_t m_26817 = x_26815 - 1;
        bool zzero_leq_i_p_m_t_s_26818 = sle32(0, m_26817);
        bool i_p_m_t_s_leq_w_26819 = slt32(m_26817, last_offset_26800);
        bool y_26820 = zzero_leq_i_p_m_t_s_26818 && i_p_m_t_s_leq_w_26819;
        bool ok_or_empty_26821 = empty_slice_26816 || y_26820;
        bool index_certs_26822;
        
        if (!ok_or_empty_26821) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:158:39-158:43 -> tupleTest.fut:74:13-74:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_26800,
                                   "] out of bounds for array of shape [",
                                   last_offset_26800, "].");
            memblock_unref(ctx, &mem_28691, "mem_28691");
            memblock_unref(ctx, &mem_28688, "mem_28688");
            memblock_unref(ctx, &mem_28685, "mem_28685");
            memblock_unref(ctx, &mem_28674, "mem_28674");
            memblock_unref(ctx, &mem_28671, "mem_28671");
            memblock_unref(ctx, &mem_28666, "mem_28666");
            memblock_unref(ctx, &mem_28661, "mem_28661");
            memblock_unref(ctx, &mem_28579, "mem_28579");
            memblock_unref(ctx, &mem_28576, "mem_28576");
            memblock_unref(ctx, &mem_28573, "mem_28573");
            memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
            memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
            memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
            memblock_unref(ctx, &mem_28542, "mem_28542");
            memblock_unref(ctx, &mem_28539, "mem_28539");
            memblock_unref(ctx, &mem_28536, "mem_28536");
            memblock_unref(ctx, &mem_28515, "mem_28515");
            memblock_unref(ctx, &mem_28512, "mem_28512");
            memblock_unref(ctx, &mem_28509, "mem_28509");
            memblock_unref(ctx, &mem_28498, "mem_28498");
            memblock_unref(ctx, &mem_28495, "mem_28495");
            memblock_unref(ctx, &mem_28490, "mem_28490");
            memblock_unref(ctx, &mem_28485, "mem_28485");
            memblock_unref(ctx, &mem_28482, "mem_28482");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        int32_t res_26826;
        int32_t redout_27734 = x_26815;
        
        for (int32_t i_27735 = 0; i_27735 < x_26815; i_27735++) {
            int32_t x_26830 = *(int32_t *) &mem_28685.mem[i_27735 * 4];
            int32_t x_26831 = *(int32_t *) &mem_28688.mem[i_27735 * 4];
            bool cond_26833 = x_26830 == 0;
            bool cond_26834 = x_26831 == 1;
            bool eq_26835 = cond_26833 && cond_26834;
            int32_t res_26836;
            
            if (eq_26835) {
                res_26836 = i_27735;
            } else {
                res_26836 = x_26815;
            }
            
            int32_t res_26829 = smin32(res_26836, redout_27734);
            int32_t redout_tmp_29208 = res_26829;
            
            redout_27734 = redout_tmp_29208;
        }
        res_26826 = redout_27734;
        memblock_unref(ctx, &mem_28685, "mem_28685");
        memblock_unref(ctx, &mem_28688, "mem_28688");
        
        bool cond_26837 = res_26826 == x_26815;
        int32_t res_26838;
        
        if (cond_26837) {
            res_26838 = -1;
        } else {
            res_26838 = res_26826;
        }
        
        bool eq_x_zz_26839 = -1 == res_26826;
        bool not_p_26840 = !cond_26837;
        bool p_and_eq_x_y_26841 = eq_x_zz_26839 && not_p_26840;
        bool cond_26842 = cond_26837 || p_and_eq_x_y_26841;
        int32_t res_26843;
        
        if (cond_26842) {
            res_26843 = 0;
        } else {
            int32_t res_26844 = *(int32_t *) &mem_28691.mem[res_26838 * 4];
            
            res_26843 = res_26844;
        }
        memblock_unref(ctx, &mem_28691, "mem_28691");
        
        bool res_26845 = res_26843 == 1;
        
        res_26785 = res_26845;
        memblock_unref(ctx, &mem_28691, "mem_28691");
        memblock_unref(ctx, &mem_28688, "mem_28688");
        memblock_unref(ctx, &mem_28685, "mem_28685");
        memblock_unref(ctx, &mem_28674, "mem_28674");
        memblock_unref(ctx, &mem_28671, "mem_28671");
        memblock_unref(ctx, &mem_28666, "mem_28666");
        memblock_unref(ctx, &mem_28661, "mem_28661");
    } else {
        res_26785 = 0;
    }
    
    struct memblock mem_28712;
    
    mem_28712.references = NULL;
    memblock_alloc(ctx, &mem_28712, 16, "mem_28712");
    
    struct memblock mem_28717;
    
    mem_28717.references = NULL;
    memblock_alloc(ctx, &mem_28717, 8, "mem_28717");
    for (int32_t i_27738 = 0; i_27738 < 2; i_27738++) {
        for (int32_t i_29210 = 0; i_29210 < 2; i_29210++) {
            *(int32_t *) &mem_28717.mem[i_29210 * 4] = i_27738;
        }
        memmove(mem_28712.mem + 2 * i_27738 * 4, mem_28717.mem + 0, 2 *
                sizeof(int32_t));
    }
    memblock_unref(ctx, &mem_28717, "mem_28717");
    
    struct memblock mem_28722;
    
    mem_28722.references = NULL;
    memblock_alloc(ctx, &mem_28722, 16, "mem_28722");
    
    struct memblock mem_28725;
    
    mem_28725.references = NULL;
    memblock_alloc(ctx, &mem_28725, 16, "mem_28725");
    
    int32_t discard_27748;
    int32_t scanacc_27742 = 0;
    
    for (int32_t i_27745 = 0; i_27745 < 4; i_27745++) {
        bool not_arg_26856 = i_27745 == 0;
        bool res_26857 = !not_arg_26856;
        int32_t part_res_26858;
        
        if (res_26857) {
            part_res_26858 = 0;
        } else {
            part_res_26858 = 1;
        }
        
        int32_t part_res_26859;
        
        if (res_26857) {
            part_res_26859 = 1;
        } else {
            part_res_26859 = 0;
        }
        
        int32_t zz_26854 = part_res_26859 + scanacc_27742;
        
        *(int32_t *) &mem_28722.mem[i_27745 * 4] = zz_26854;
        *(int32_t *) &mem_28725.mem[i_27745 * 4] = part_res_26858;
        
        int32_t scanacc_tmp_29211 = zz_26854;
        
        scanacc_27742 = scanacc_tmp_29211;
    }
    discard_27748 = scanacc_27742;
    
    int32_t last_offset_26860 = *(int32_t *) &mem_28722.mem[12];
    int64_t binop_x_28735 = sext_i32_i64(last_offset_26860);
    int64_t bytes_28734 = 4 * binop_x_28735;
    struct memblock mem_28736;
    
    mem_28736.references = NULL;
    memblock_alloc(ctx, &mem_28736, bytes_28734, "mem_28736");
    
    struct memblock mem_28739;
    
    mem_28739.references = NULL;
    memblock_alloc(ctx, &mem_28739, bytes_28734, "mem_28739");
    
    struct memblock mem_28742;
    
    mem_28742.references = NULL;
    memblock_alloc(ctx, &mem_28742, bytes_28734, "mem_28742");
    for (int32_t write_iter_27749 = 0; write_iter_27749 < 4;
         write_iter_27749++) {
        int32_t write_iv_27753 = *(int32_t *) &mem_28725.mem[write_iter_27749 *
                                                             4];
        int32_t write_iv_27754 = *(int32_t *) &mem_28722.mem[write_iter_27749 *
                                                             4];
        int32_t new_index_28182 = squot32(write_iter_27749, 2);
        int32_t binop_y_28184 = 2 * new_index_28182;
        int32_t new_index_28185 = write_iter_27749 - binop_y_28184;
        int32_t write_iv_27755 = *(int32_t *) &mem_28712.mem[(new_index_28182 *
                                                              2 +
                                                              new_index_28185) *
                                                             4];
        bool is_this_one_26872 = write_iv_27753 == 0;
        int32_t this_offset_26873 = -1 + write_iv_27754;
        int32_t total_res_26874;
        
        if (is_this_one_26872) {
            total_res_26874 = this_offset_26873;
        } else {
            total_res_26874 = -1;
        }
        
        bool less_than_zzero_27758 = slt32(total_res_26874, 0);
        bool greater_than_sizze_27759 = sle32(last_offset_26860,
                                              total_res_26874);
        bool outside_bounds_dim_27760 = less_than_zzero_27758 ||
             greater_than_sizze_27759;
        
        if (!outside_bounds_dim_27760) {
            *(int32_t *) &mem_28736.mem[total_res_26874 * 4] = write_iv_27755;
        }
        if (!outside_bounds_dim_27760) {
            *(int32_t *) &mem_28739.mem[total_res_26874 * 4] = new_index_28185;
        }
        if (!outside_bounds_dim_27760) {
            *(int32_t *) &mem_28742.mem[total_res_26874 * 4] = write_iter_27749;
        }
    }
    memblock_unref(ctx, &mem_28712, "mem_28712");
    memblock_unref(ctx, &mem_28722, "mem_28722");
    memblock_unref(ctx, &mem_28725, "mem_28725");
    
    int32_t x_26875 = abs(last_offset_26860);
    bool empty_slice_26876 = x_26875 == 0;
    int32_t m_26877 = x_26875 - 1;
    bool zzero_leq_i_p_m_t_s_26878 = sle32(0, m_26877);
    bool i_p_m_t_s_leq_w_26879 = slt32(m_26877, last_offset_26860);
    bool y_26880 = zzero_leq_i_p_m_t_s_26878 && i_p_m_t_s_leq_w_26879;
    bool ok_or_empty_26881 = empty_slice_26876 || y_26880;
    bool index_certs_26882;
    
    if (!ok_or_empty_26881) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:159:15-159:19 -> tupleTest.fut:82:13-82:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", last_offset_26860,
                               "] out of bounds for array of shape [",
                               last_offset_26860, "].");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    struct memblock mem_28763;
    
    mem_28763.references = NULL;
    memblock_alloc(ctx, &mem_28763, 16, "mem_28763");
    for (int32_t i_29217 = 0; i_29217 < 4; i_29217++) {
        *(int32_t *) &mem_28763.mem[i_29217 * 4] = 0;
    }
    for (int32_t write_iter_27776 = 0; write_iter_27776 < x_26875;
         write_iter_27776++) {
        int32_t write_iv_27778 = *(int32_t *) &mem_28739.mem[write_iter_27776 *
                                                             4];
        int32_t write_iv_27779 = *(int32_t *) &mem_28736.mem[write_iter_27776 *
                                                             4];
        int32_t write_iv_27780 = *(int32_t *) &mem_28742.mem[write_iter_27776 *
                                                             4];
        int32_t x_26891 = 2 * write_iv_27778;
        int32_t res_26892 = x_26891 + write_iv_27779;
        bool less_than_zzero_27781 = slt32(res_26892, 0);
        bool greater_than_sizze_27782 = sle32(4, res_26892);
        bool outside_bounds_dim_27783 = less_than_zzero_27781 ||
             greater_than_sizze_27782;
        
        if (!outside_bounds_dim_27783) {
            *(int32_t *) &mem_28763.mem[res_26892 * 4] = write_iv_27780;
        }
    }
    memblock_unref(ctx, &mem_28736, "mem_28736");
    memblock_unref(ctx, &mem_28739, "mem_28739");
    memblock_unref(ctx, &mem_28742, "mem_28742");
    
    bool res_26893;
    bool redout_27787 = 1;
    
    for (int32_t i_27788 = 0; i_27788 < 2; i_27788++) {
        int32_t binop_x_26898 = 2 * i_27788;
        int32_t new_index_26899 = binop_x_26898 + i_27788;
        int32_t y_26900 = *(int32_t *) &mem_28763.mem[new_index_26899 * 4];
        bool res_26901 = new_index_26899 == y_26900;
        bool x_26896 = res_26901 && redout_27787;
        bool redout_tmp_29219 = x_26896;
        
        redout_27787 = redout_tmp_29219;
    }
    res_26893 = redout_27787;
    memblock_unref(ctx, &mem_28763, "mem_28763");
    
    bool cond_26902;
    
    if (res_26893) {
        struct memblock mem_28772;
        
        mem_28772.references = NULL;
        memblock_alloc(ctx, &mem_28772, 16, "mem_28772");
        
        struct memblock mem_28777;
        
        mem_28777.references = NULL;
        memblock_alloc(ctx, &mem_28777, 8, "mem_28777");
        for (int32_t i_27791 = 0; i_27791 < 2; i_27791++) {
            for (int32_t i_29221 = 0; i_29221 < 2; i_29221++) {
                *(int32_t *) &mem_28777.mem[i_29221 * 4] = i_27791;
            }
            memmove(mem_28772.mem + 2 * i_27791 * 4, mem_28777.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_28777, "mem_28777");
        
        struct memblock mem_28782;
        
        mem_28782.references = NULL;
        memblock_alloc(ctx, &mem_28782, 16, "mem_28782");
        
        struct memblock mem_28785;
        
        mem_28785.references = NULL;
        memblock_alloc(ctx, &mem_28785, 16, "mem_28785");
        
        int32_t discard_27801;
        int32_t scanacc_27795 = 0;
        
        for (int32_t i_27798 = 0; i_27798 < 4; i_27798++) {
            bool not_arg_26913 = i_27798 == 0;
            bool res_26914 = !not_arg_26913;
            int32_t part_res_26915;
            
            if (res_26914) {
                part_res_26915 = 0;
            } else {
                part_res_26915 = 1;
            }
            
            int32_t part_res_26916;
            
            if (res_26914) {
                part_res_26916 = 1;
            } else {
                part_res_26916 = 0;
            }
            
            int32_t zz_26911 = part_res_26916 + scanacc_27795;
            
            *(int32_t *) &mem_28782.mem[i_27798 * 4] = zz_26911;
            *(int32_t *) &mem_28785.mem[i_27798 * 4] = part_res_26915;
            
            int32_t scanacc_tmp_29222 = zz_26911;
            
            scanacc_27795 = scanacc_tmp_29222;
        }
        discard_27801 = scanacc_27795;
        
        int32_t last_offset_26917 = *(int32_t *) &mem_28782.mem[12];
        int64_t binop_x_28795 = sext_i32_i64(last_offset_26917);
        int64_t bytes_28794 = 4 * binop_x_28795;
        struct memblock mem_28796;
        
        mem_28796.references = NULL;
        memblock_alloc(ctx, &mem_28796, bytes_28794, "mem_28796");
        
        struct memblock mem_28799;
        
        mem_28799.references = NULL;
        memblock_alloc(ctx, &mem_28799, bytes_28794, "mem_28799");
        
        struct memblock mem_28802;
        
        mem_28802.references = NULL;
        memblock_alloc(ctx, &mem_28802, bytes_28794, "mem_28802");
        for (int32_t write_iter_27802 = 0; write_iter_27802 < 4;
             write_iter_27802++) {
            int32_t write_iv_27806 =
                    *(int32_t *) &mem_28785.mem[write_iter_27802 * 4];
            int32_t write_iv_27807 =
                    *(int32_t *) &mem_28782.mem[write_iter_27802 * 4];
            int32_t new_index_28201 = squot32(write_iter_27802, 2);
            int32_t binop_y_28203 = 2 * new_index_28201;
            int32_t new_index_28204 = write_iter_27802 - binop_y_28203;
            int32_t write_iv_27808 =
                    *(int32_t *) &mem_28772.mem[(new_index_28201 * 2 +
                                                 new_index_28204) * 4];
            bool is_this_one_26929 = write_iv_27806 == 0;
            int32_t this_offset_26930 = -1 + write_iv_27807;
            int32_t total_res_26931;
            
            if (is_this_one_26929) {
                total_res_26931 = this_offset_26930;
            } else {
                total_res_26931 = -1;
            }
            
            bool less_than_zzero_27811 = slt32(total_res_26931, 0);
            bool greater_than_sizze_27812 = sle32(last_offset_26917,
                                                  total_res_26931);
            bool outside_bounds_dim_27813 = less_than_zzero_27811 ||
                 greater_than_sizze_27812;
            
            if (!outside_bounds_dim_27813) {
                *(int32_t *) &mem_28796.mem[total_res_26931 * 4] =
                    write_iv_27808;
            }
            if (!outside_bounds_dim_27813) {
                *(int32_t *) &mem_28799.mem[total_res_26931 * 4] =
                    new_index_28204;
            }
            if (!outside_bounds_dim_27813) {
                *(int32_t *) &mem_28802.mem[total_res_26931 * 4] =
                    write_iter_27802;
            }
        }
        memblock_unref(ctx, &mem_28772, "mem_28772");
        memblock_unref(ctx, &mem_28782, "mem_28782");
        memblock_unref(ctx, &mem_28785, "mem_28785");
        
        int32_t x_26932 = abs(last_offset_26917);
        bool empty_slice_26933 = x_26932 == 0;
        int32_t m_26934 = x_26932 - 1;
        bool zzero_leq_i_p_m_t_s_26935 = sle32(0, m_26934);
        bool i_p_m_t_s_leq_w_26936 = slt32(m_26934, last_offset_26917);
        bool y_26937 = zzero_leq_i_p_m_t_s_26935 && i_p_m_t_s_leq_w_26936;
        bool ok_or_empty_26938 = empty_slice_26933 || y_26937;
        bool index_certs_26939;
        
        if (!ok_or_empty_26938) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:159:24-159:29 -> tupleTest.fut:89:13-89:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_26917,
                                   "] out of bounds for array of shape [",
                                   last_offset_26917, "].");
            memblock_unref(ctx, &mem_28802, "mem_28802");
            memblock_unref(ctx, &mem_28799, "mem_28799");
            memblock_unref(ctx, &mem_28796, "mem_28796");
            memblock_unref(ctx, &mem_28785, "mem_28785");
            memblock_unref(ctx, &mem_28782, "mem_28782");
            memblock_unref(ctx, &mem_28777, "mem_28777");
            memblock_unref(ctx, &mem_28772, "mem_28772");
            memblock_unref(ctx, &mem_28763, "mem_28763");
            memblock_unref(ctx, &mem_28742, "mem_28742");
            memblock_unref(ctx, &mem_28739, "mem_28739");
            memblock_unref(ctx, &mem_28736, "mem_28736");
            memblock_unref(ctx, &mem_28725, "mem_28725");
            memblock_unref(ctx, &mem_28722, "mem_28722");
            memblock_unref(ctx, &mem_28717, "mem_28717");
            memblock_unref(ctx, &mem_28712, "mem_28712");
            memblock_unref(ctx, &mem_28579, "mem_28579");
            memblock_unref(ctx, &mem_28576, "mem_28576");
            memblock_unref(ctx, &mem_28573, "mem_28573");
            memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
            memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
            memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
            memblock_unref(ctx, &mem_28542, "mem_28542");
            memblock_unref(ctx, &mem_28539, "mem_28539");
            memblock_unref(ctx, &mem_28536, "mem_28536");
            memblock_unref(ctx, &mem_28515, "mem_28515");
            memblock_unref(ctx, &mem_28512, "mem_28512");
            memblock_unref(ctx, &mem_28509, "mem_28509");
            memblock_unref(ctx, &mem_28498, "mem_28498");
            memblock_unref(ctx, &mem_28495, "mem_28495");
            memblock_unref(ctx, &mem_28490, "mem_28490");
            memblock_unref(ctx, &mem_28485, "mem_28485");
            memblock_unref(ctx, &mem_28482, "mem_28482");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        struct memblock mem_28823;
        
        mem_28823.references = NULL;
        memblock_alloc(ctx, &mem_28823, 16, "mem_28823");
        for (int32_t i_29228 = 0; i_29228 < 4; i_29228++) {
            *(int32_t *) &mem_28823.mem[i_29228 * 4] = 0;
        }
        for (int32_t write_iter_27829 = 0; write_iter_27829 < x_26932;
             write_iter_27829++) {
            int32_t write_iv_27831 =
                    *(int32_t *) &mem_28796.mem[write_iter_27829 * 4];
            int32_t write_iv_27832 =
                    *(int32_t *) &mem_28799.mem[write_iter_27829 * 4];
            int32_t write_iv_27833 =
                    *(int32_t *) &mem_28802.mem[write_iter_27829 * 4];
            int32_t x_26948 = 2 * write_iv_27831;
            int32_t res_26949 = x_26948 + write_iv_27832;
            bool less_than_zzero_27834 = slt32(res_26949, 0);
            bool greater_than_sizze_27835 = sle32(4, res_26949);
            bool outside_bounds_dim_27836 = less_than_zzero_27834 ||
                 greater_than_sizze_27835;
            
            if (!outside_bounds_dim_27836) {
                *(int32_t *) &mem_28823.mem[res_26949 * 4] = write_iv_27833;
            }
        }
        memblock_unref(ctx, &mem_28796, "mem_28796");
        memblock_unref(ctx, &mem_28799, "mem_28799");
        memblock_unref(ctx, &mem_28802, "mem_28802");
        
        bool all_equal_26950;
        bool redout_27840 = 1;
        
        for (int32_t i_27841 = 0; i_27841 < 4; i_27841++) {
            int32_t x_26954 = *(int32_t *) &mem_28823.mem[i_27841 * 4];
            bool res_26956 = x_26954 == i_27841;
            bool res_26953 = res_26956 && redout_27840;
            bool redout_tmp_29230 = res_26953;
            
            redout_27840 = redout_tmp_29230;
        }
        all_equal_26950 = redout_27840;
        memblock_unref(ctx, &mem_28823, "mem_28823");
        cond_26902 = all_equal_26950;
        memblock_unref(ctx, &mem_28823, "mem_28823");
        memblock_unref(ctx, &mem_28802, "mem_28802");
        memblock_unref(ctx, &mem_28799, "mem_28799");
        memblock_unref(ctx, &mem_28796, "mem_28796");
        memblock_unref(ctx, &mem_28785, "mem_28785");
        memblock_unref(ctx, &mem_28782, "mem_28782");
        memblock_unref(ctx, &mem_28777, "mem_28777");
        memblock_unref(ctx, &mem_28772, "mem_28772");
    } else {
        cond_26902 = 0;
    }
    
    bool res_26957;
    
    if (cond_26902) {
        struct memblock mem_28832;
        
        mem_28832.references = NULL;
        memblock_alloc(ctx, &mem_28832, 36, "mem_28832");
        for (int32_t i_29231 = 0; i_29231 < 9; i_29231++) {
            *(int32_t *) &mem_28832.mem[i_29231 * 4] = 0;
        }
        
        struct memblock mem_28835;
        
        mem_28835.references = NULL;
        memblock_alloc(ctx, &mem_28835, 36, "mem_28835");
        for (int32_t i_29232 = 0; i_29232 < 9; i_29232++) {
            *(int32_t *) &mem_28835.mem[i_29232 * 4] = 0;
        }
        for (int32_t write_iter_27842 = 0; write_iter_27842 < 3;
             write_iter_27842++) {
            int32_t x_26964 = 3 * write_iter_27842;
            int32_t res_26965 = x_26964 + write_iter_27842;
            
            *(int32_t *) &mem_28835.mem[res_26965 * 4] = 1;
            *(int32_t *) &mem_28832.mem[res_26965 * 4] = 1;
        }
        
        bool all_equal_26966;
        bool redout_27858 = 1;
        
        for (int32_t i_27859 = 0; i_27859 < 9; i_27859++) {
            int32_t x_26970 = *(int32_t *) &mem_28835.mem[i_27859 * 4];
            int32_t y_26971 = *(int32_t *) &mem_28832.mem[i_27859 * 4];
            bool res_26972 = x_26970 == y_26971;
            bool res_26969 = res_26972 && redout_27858;
            bool redout_tmp_29235 = res_26969;
            
            redout_27858 = redout_tmp_29235;
        }
        all_equal_26966 = redout_27858;
        memblock_unref(ctx, &mem_28832, "mem_28832");
        memblock_unref(ctx, &mem_28835, "mem_28835");
        res_26957 = all_equal_26966;
        memblock_unref(ctx, &mem_28835, "mem_28835");
        memblock_unref(ctx, &mem_28832, "mem_28832");
    } else {
        res_26957 = 0;
    }
    
    struct memblock mem_28846;
    
    mem_28846.references = NULL;
    memblock_alloc(ctx, &mem_28846, 16, "mem_28846");
    
    struct memblock mem_28851;
    
    mem_28851.references = NULL;
    memblock_alloc(ctx, &mem_28851, 8, "mem_28851");
    for (int32_t i_27867 = 0; i_27867 < 2; i_27867++) {
        for (int32_t i_29237 = 0; i_29237 < 2; i_29237++) {
            *(int32_t *) &mem_28851.mem[i_29237 * 4] = i_27867;
        }
        memmove(mem_28846.mem + 2 * i_27867 * 4, mem_28851.mem + 0, 2 *
                sizeof(int32_t));
    }
    memblock_unref(ctx, &mem_28851, "mem_28851");
    
    struct memblock mem_28856;
    
    mem_28856.references = NULL;
    memblock_alloc(ctx, &mem_28856, 16, "mem_28856");
    
    int32_t discard_27874;
    int32_t scanacc_27870 = 0;
    
    for (int32_t i_27872 = 0; i_27872 < 4; i_27872++) {
        int32_t zz_26998 = 1 + scanacc_27870;
        
        *(int32_t *) &mem_28856.mem[i_27872 * 4] = zz_26998;
        
        int32_t scanacc_tmp_29238 = zz_26998;
        
        scanacc_27870 = scanacc_tmp_29238;
    }
    discard_27874 = scanacc_27870;
    
    int32_t last_offset_27000 = *(int32_t *) &mem_28856.mem[12];
    int64_t binop_x_28862 = sext_i32_i64(last_offset_27000);
    int64_t bytes_28861 = 4 * binop_x_28862;
    struct memblock mem_28863;
    
    mem_28863.references = NULL;
    memblock_alloc(ctx, &mem_28863, bytes_28861, "mem_28863");
    
    struct memblock mem_28866;
    
    mem_28866.references = NULL;
    memblock_alloc(ctx, &mem_28866, bytes_28861, "mem_28866");
    
    struct memblock mem_28869;
    
    mem_28869.references = NULL;
    memblock_alloc(ctx, &mem_28869, bytes_28861, "mem_28869");
    for (int32_t write_iter_27875 = 0; write_iter_27875 < 4;
         write_iter_27875++) {
        int32_t write_iv_27879 = *(int32_t *) &mem_28856.mem[write_iter_27875 *
                                                             4];
        int32_t new_index_28220 = squot32(write_iter_27875, 2);
        int32_t binop_y_28222 = 2 * new_index_28220;
        int32_t new_index_28223 = write_iter_27875 - binop_y_28222;
        int32_t write_iv_27880 = *(int32_t *) &mem_28846.mem[(new_index_28220 *
                                                              2 +
                                                              new_index_28223) *
                                                             4];
        int32_t this_offset_27010 = -1 + write_iv_27879;
        bool less_than_zzero_27882 = slt32(this_offset_27010, 0);
        bool greater_than_sizze_27883 = sle32(last_offset_27000,
                                              this_offset_27010);
        bool outside_bounds_dim_27884 = less_than_zzero_27882 ||
             greater_than_sizze_27883;
        
        if (!outside_bounds_dim_27884) {
            *(int32_t *) &mem_28863.mem[this_offset_27010 * 4] = write_iv_27880;
        }
        if (!outside_bounds_dim_27884) {
            *(int32_t *) &mem_28866.mem[this_offset_27010 * 4] =
                new_index_28223;
        }
        if (!outside_bounds_dim_27884) {
            *(int32_t *) &mem_28869.mem[this_offset_27010 * 4] = 1;
        }
    }
    memblock_unref(ctx, &mem_28846, "mem_28846");
    memblock_unref(ctx, &mem_28856, "mem_28856");
    
    int32_t x_27011 = abs(last_offset_27000);
    bool empty_slice_27012 = x_27011 == 0;
    int32_t m_27013 = x_27011 - 1;
    bool zzero_leq_i_p_m_t_s_27014 = sle32(0, m_27013);
    bool i_p_m_t_s_leq_w_27015 = slt32(m_27013, last_offset_27000);
    bool y_27016 = zzero_leq_i_p_m_t_s_27014 && i_p_m_t_s_leq_w_27015;
    bool ok_or_empty_27017 = empty_slice_27012 || y_27016;
    bool index_certs_27018;
    
    if (!ok_or_empty_27017) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:161:24-161:29 -> tupleTest.fut:114:13-114:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", last_offset_27000,
                               "] out of bounds for array of shape [",
                               last_offset_27000, "].");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    struct memblock mem_28890;
    
    mem_28890.references = NULL;
    memblock_alloc(ctx, &mem_28890, 16, "mem_28890");
    for (int32_t i_29243 = 0; i_29243 < 4; i_29243++) {
        *(int32_t *) &mem_28890.mem[i_29243 * 4] = 0;
    }
    for (int32_t write_iter_27900 = 0; write_iter_27900 < x_27011;
         write_iter_27900++) {
        int32_t write_iv_27902 = *(int32_t *) &mem_28869.mem[write_iter_27900 *
                                                             4];
        int32_t write_iv_27903 = *(int32_t *) &mem_28863.mem[write_iter_27900 *
                                                             4];
        int32_t write_iv_27904 = *(int32_t *) &mem_28866.mem[write_iter_27900 *
                                                             4];
        int32_t res_27027 = 2 * write_iv_27902;
        int32_t x_27028 = 2 * write_iv_27903;
        int32_t res_27029 = x_27028 + write_iv_27904;
        bool less_than_zzero_27905 = slt32(res_27029, 0);
        bool greater_than_sizze_27906 = sle32(4, res_27029);
        bool outside_bounds_dim_27907 = less_than_zzero_27905 ||
             greater_than_sizze_27906;
        
        if (!outside_bounds_dim_27907) {
            *(int32_t *) &mem_28890.mem[res_27029 * 4] = res_27027;
        }
    }
    memblock_unref(ctx, &mem_28863, "mem_28863");
    memblock_unref(ctx, &mem_28866, "mem_28866");
    memblock_unref(ctx, &mem_28869, "mem_28869");
    
    bool res_27030;
    bool redout_27911 = 1;
    
    for (int32_t i_27912 = 0; i_27912 < 4; i_27912++) {
        int32_t x_27034 = *(int32_t *) &mem_28890.mem[i_27912 * 4];
        bool res_27035 = x_27034 == 2;
        bool x_27033 = res_27035 && redout_27911;
        bool redout_tmp_29245 = x_27033;
        
        redout_27911 = redout_tmp_29245;
    }
    res_27030 = redout_27911;
    memblock_unref(ctx, &mem_28890, "mem_28890");
    
    bool res_27036;
    
    if (res_27030) {
        struct memblock mem_28899;
        
        mem_28899.references = NULL;
        memblock_alloc(ctx, &mem_28899, 16, "mem_28899");
        
        struct memblock mem_28902;
        
        mem_28902.references = NULL;
        memblock_alloc(ctx, &mem_28902, 16, "mem_28902");
        
        int32_t discard_27925;
        int32_t scanacc_27919 = 0;
        
        for (int32_t i_27922 = 0; i_27922 < 4; i_27922++) {
            bool not_arg_27047 = i_27922 == 0;
            bool res_27048 = !not_arg_27047;
            int32_t part_res_27049;
            
            if (res_27048) {
                part_res_27049 = 0;
            } else {
                part_res_27049 = 1;
            }
            
            int32_t part_res_27050;
            
            if (res_27048) {
                part_res_27050 = 1;
            } else {
                part_res_27050 = 0;
            }
            
            int32_t zz_27045 = part_res_27050 + scanacc_27919;
            
            *(int32_t *) &mem_28899.mem[i_27922 * 4] = zz_27045;
            *(int32_t *) &mem_28902.mem[i_27922 * 4] = part_res_27049;
            
            int32_t scanacc_tmp_29246 = zz_27045;
            
            scanacc_27919 = scanacc_tmp_29246;
        }
        discard_27925 = scanacc_27919;
        
        int32_t last_offset_27051 = *(int32_t *) &mem_28899.mem[12];
        int64_t binop_x_28912 = sext_i32_i64(last_offset_27051);
        int64_t bytes_28911 = 4 * binop_x_28912;
        struct memblock mem_28913;
        
        mem_28913.references = NULL;
        memblock_alloc(ctx, &mem_28913, bytes_28911, "mem_28913");
        for (int32_t write_iter_27926 = 0; write_iter_27926 < 4;
             write_iter_27926++) {
            int32_t write_iv_27928 =
                    *(int32_t *) &mem_28902.mem[write_iter_27926 * 4];
            int32_t write_iv_27929 =
                    *(int32_t *) &mem_28899.mem[write_iter_27926 * 4];
            bool is_this_one_27059 = write_iv_27928 == 0;
            int32_t this_offset_27060 = -1 + write_iv_27929;
            int32_t total_res_27061;
            
            if (is_this_one_27059) {
                total_res_27061 = this_offset_27060;
            } else {
                total_res_27061 = -1;
            }
            
            bool less_than_zzero_27933 = slt32(total_res_27061, 0);
            bool greater_than_sizze_27934 = sle32(last_offset_27051,
                                                  total_res_27061);
            bool outside_bounds_dim_27935 = less_than_zzero_27933 ||
                 greater_than_sizze_27934;
            
            if (!outside_bounds_dim_27935) {
                *(int32_t *) &mem_28913.mem[total_res_27061 * 4] =
                    write_iter_27926;
            }
        }
        memblock_unref(ctx, &mem_28899, "mem_28899");
        memblock_unref(ctx, &mem_28902, "mem_28902");
        
        int32_t x_27062 = abs(last_offset_27051);
        bool empty_slice_27063 = x_27062 == 0;
        int32_t m_27064 = x_27062 - 1;
        bool zzero_leq_i_p_m_t_s_27065 = sle32(0, m_27064);
        bool i_p_m_t_s_leq_w_27066 = slt32(m_27064, last_offset_27051);
        bool y_27067 = zzero_leq_i_p_m_t_s_27065 && i_p_m_t_s_leq_w_27066;
        bool ok_or_empty_27068 = empty_slice_27063 || y_27067;
        bool index_certs_27069;
        
        if (!ok_or_empty_27068) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:161:34-161:39 -> tupleTest.fut:123:13-123:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_27051,
                                   "] out of bounds for array of shape [",
                                   last_offset_27051, "].");
            memblock_unref(ctx, &mem_28913, "mem_28913");
            memblock_unref(ctx, &mem_28902, "mem_28902");
            memblock_unref(ctx, &mem_28899, "mem_28899");
            memblock_unref(ctx, &mem_28890, "mem_28890");
            memblock_unref(ctx, &mem_28869, "mem_28869");
            memblock_unref(ctx, &mem_28866, "mem_28866");
            memblock_unref(ctx, &mem_28863, "mem_28863");
            memblock_unref(ctx, &mem_28856, "mem_28856");
            memblock_unref(ctx, &mem_28851, "mem_28851");
            memblock_unref(ctx, &mem_28846, "mem_28846");
            memblock_unref(ctx, &mem_28763, "mem_28763");
            memblock_unref(ctx, &mem_28742, "mem_28742");
            memblock_unref(ctx, &mem_28739, "mem_28739");
            memblock_unref(ctx, &mem_28736, "mem_28736");
            memblock_unref(ctx, &mem_28725, "mem_28725");
            memblock_unref(ctx, &mem_28722, "mem_28722");
            memblock_unref(ctx, &mem_28717, "mem_28717");
            memblock_unref(ctx, &mem_28712, "mem_28712");
            memblock_unref(ctx, &mem_28579, "mem_28579");
            memblock_unref(ctx, &mem_28576, "mem_28576");
            memblock_unref(ctx, &mem_28573, "mem_28573");
            memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
            memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
            memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
            memblock_unref(ctx, &mem_28542, "mem_28542");
            memblock_unref(ctx, &mem_28539, "mem_28539");
            memblock_unref(ctx, &mem_28536, "mem_28536");
            memblock_unref(ctx, &mem_28515, "mem_28515");
            memblock_unref(ctx, &mem_28512, "mem_28512");
            memblock_unref(ctx, &mem_28509, "mem_28509");
            memblock_unref(ctx, &mem_28498, "mem_28498");
            memblock_unref(ctx, &mem_28495, "mem_28495");
            memblock_unref(ctx, &mem_28490, "mem_28490");
            memblock_unref(ctx, &mem_28485, "mem_28485");
            memblock_unref(ctx, &mem_28482, "mem_28482");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        bool dim_eq_27072 = 3 == x_27062;
        bool arrays_equal_27073;
        
        if (dim_eq_27072) {
            bool all_equal_27075;
            bool redout_27939 = 1;
            
            for (int32_t i_27940 = 0; i_27940 < 3; i_27940++) {
                int32_t x_27079 = *(int32_t *) &mem_28913.mem[i_27940 * 4];
                int32_t res_27081 = 2 * x_27079;
                int32_t res_27082 = 1 + i_27940;
                int32_t res_27083 = 2 * res_27082;
                bool res_27084 = res_27083 == res_27081;
                bool res_27078 = res_27084 && redout_27939;
                bool redout_tmp_29250 = res_27078;
                
                redout_27939 = redout_tmp_29250;
            }
            all_equal_27075 = redout_27939;
            arrays_equal_27073 = all_equal_27075;
        } else {
            arrays_equal_27073 = 0;
        }
        memblock_unref(ctx, &mem_28913, "mem_28913");
        res_27036 = arrays_equal_27073;
        memblock_unref(ctx, &mem_28913, "mem_28913");
        memblock_unref(ctx, &mem_28902, "mem_28902");
        memblock_unref(ctx, &mem_28899, "mem_28899");
    } else {
        res_27036 = 0;
    }
    
    struct memblock mem_28922;
    
    mem_28922.references = NULL;
    memblock_alloc(ctx, &mem_28922, 24, "mem_28922");
    
    struct memblock mem_28927;
    
    mem_28927.references = NULL;
    memblock_alloc(ctx, &mem_28927, 8, "mem_28927");
    for (int32_t i_27943 = 0; i_27943 < 3; i_27943++) {
        for (int32_t i_29252 = 0; i_29252 < 2; i_29252++) {
            *(int32_t *) &mem_28927.mem[i_29252 * 4] = i_27943;
        }
        memmove(mem_28922.mem + 2 * i_27943 * 4, mem_28927.mem + 0, 2 *
                sizeof(int32_t));
    }
    memblock_unref(ctx, &mem_28927, "mem_28927");
    
    struct memblock mem_28932;
    
    mem_28932.references = NULL;
    memblock_alloc(ctx, &mem_28932, 24, "mem_28932");
    
    struct memblock mem_28935;
    
    mem_28935.references = NULL;
    memblock_alloc(ctx, &mem_28935, 24, "mem_28935");
    
    int32_t discard_27953;
    int32_t scanacc_27947 = 0;
    
    for (int32_t i_27950 = 0; i_27950 < 6; i_27950++) {
        bool not_arg_27099 = i_27950 == 0;
        bool res_27100 = !not_arg_27099;
        int32_t part_res_27101;
        
        if (res_27100) {
            part_res_27101 = 0;
        } else {
            part_res_27101 = 1;
        }
        
        int32_t part_res_27102;
        
        if (res_27100) {
            part_res_27102 = 1;
        } else {
            part_res_27102 = 0;
        }
        
        int32_t zz_27097 = part_res_27102 + scanacc_27947;
        
        *(int32_t *) &mem_28932.mem[i_27950 * 4] = zz_27097;
        *(int32_t *) &mem_28935.mem[i_27950 * 4] = part_res_27101;
        
        int32_t scanacc_tmp_29253 = zz_27097;
        
        scanacc_27947 = scanacc_tmp_29253;
    }
    discard_27953 = scanacc_27947;
    
    int32_t last_offset_27103 = *(int32_t *) &mem_28932.mem[20];
    int64_t binop_x_28945 = sext_i32_i64(last_offset_27103);
    int64_t bytes_28944 = 4 * binop_x_28945;
    struct memblock mem_28946;
    
    mem_28946.references = NULL;
    memblock_alloc(ctx, &mem_28946, bytes_28944, "mem_28946");
    
    struct memblock mem_28949;
    
    mem_28949.references = NULL;
    memblock_alloc(ctx, &mem_28949, bytes_28944, "mem_28949");
    
    struct memblock mem_28952;
    
    mem_28952.references = NULL;
    memblock_alloc(ctx, &mem_28952, bytes_28944, "mem_28952");
    for (int32_t write_iter_27954 = 0; write_iter_27954 < 6;
         write_iter_27954++) {
        int32_t write_iv_27958 = *(int32_t *) &mem_28935.mem[write_iter_27954 *
                                                             4];
        int32_t write_iv_27959 = *(int32_t *) &mem_28932.mem[write_iter_27954 *
                                                             4];
        int32_t new_index_28251 = squot32(write_iter_27954, 2);
        int32_t binop_y_28253 = 2 * new_index_28251;
        int32_t new_index_28254 = write_iter_27954 - binop_y_28253;
        int32_t write_iv_27960 = *(int32_t *) &mem_28922.mem[(new_index_28251 *
                                                              2 +
                                                              new_index_28254) *
                                                             4];
        bool is_this_one_27115 = write_iv_27958 == 0;
        int32_t this_offset_27116 = -1 + write_iv_27959;
        int32_t total_res_27117;
        
        if (is_this_one_27115) {
            total_res_27117 = this_offset_27116;
        } else {
            total_res_27117 = -1;
        }
        
        bool less_than_zzero_27963 = slt32(total_res_27117, 0);
        bool greater_than_sizze_27964 = sle32(last_offset_27103,
                                              total_res_27117);
        bool outside_bounds_dim_27965 = less_than_zzero_27963 ||
             greater_than_sizze_27964;
        
        if (!outside_bounds_dim_27965) {
            *(int32_t *) &mem_28946.mem[total_res_27117 * 4] = write_iv_27960;
        }
        if (!outside_bounds_dim_27965) {
            *(int32_t *) &mem_28949.mem[total_res_27117 * 4] = new_index_28254;
        }
        if (!outside_bounds_dim_27965) {
            *(int32_t *) &mem_28952.mem[total_res_27117 * 4] = write_iter_27954;
        }
    }
    memblock_unref(ctx, &mem_28922, "mem_28922");
    memblock_unref(ctx, &mem_28932, "mem_28932");
    memblock_unref(ctx, &mem_28935, "mem_28935");
    
    int32_t x_27118 = abs(last_offset_27103);
    bool empty_slice_27119 = x_27118 == 0;
    int32_t m_27120 = x_27118 - 1;
    bool zzero_leq_i_p_m_t_s_27121 = sle32(0, m_27120);
    bool i_p_m_t_s_leq_w_27122 = slt32(m_27120, last_offset_27103);
    bool y_27123 = zzero_leq_i_p_m_t_s_27121 && i_p_m_t_s_leq_w_27122;
    bool ok_or_empty_27124 = empty_slice_27119 || y_27123;
    bool index_certs_27125;
    
    if (!ok_or_empty_27124) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:133:13-133:29 -> tupleSparse.fut:131:18-131:63 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", last_offset_27103,
                               "] out of bounds for array of shape [",
                               last_offset_27103, "].");
        memblock_unref(ctx, &mem_28952, "mem_28952");
        memblock_unref(ctx, &mem_28949, "mem_28949");
        memblock_unref(ctx, &mem_28946, "mem_28946");
        memblock_unref(ctx, &mem_28935, "mem_28935");
        memblock_unref(ctx, &mem_28932, "mem_28932");
        memblock_unref(ctx, &mem_28927, "mem_28927");
        memblock_unref(ctx, &mem_28922, "mem_28922");
        memblock_unref(ctx, &mem_28890, "mem_28890");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    int64_t binop_x_28972 = sext_i32_i64(x_27118);
    int64_t bytes_28971 = 4 * binop_x_28972;
    struct memblock mem_28973;
    
    mem_28973.references = NULL;
    memblock_alloc(ctx, &mem_28973, bytes_28971, "mem_28973");
    
    int32_t tmp_offs_29259 = 0;
    
    memmove(mem_28973.mem + tmp_offs_29259 * 4, mem_28482.mem + 0, 0);
    tmp_offs_29259 = tmp_offs_29259;
    memmove(mem_28973.mem + tmp_offs_29259 * 4, mem_28946.mem + 0, x_27118 *
            sizeof(int32_t));
    tmp_offs_29259 += x_27118;
    
    struct memblock mem_28976;
    
    mem_28976.references = NULL;
    memblock_alloc(ctx, &mem_28976, bytes_28971, "mem_28976");
    
    int32_t tmp_offs_29260 = 0;
    
    memmove(mem_28976.mem + tmp_offs_29260 * 4, mem_28482.mem + 0, 0);
    tmp_offs_29260 = tmp_offs_29260;
    memmove(mem_28976.mem + tmp_offs_29260 * 4, mem_28949.mem + 0, x_27118 *
            sizeof(int32_t));
    tmp_offs_29260 += x_27118;
    
    struct memblock mem_28979;
    
    mem_28979.references = NULL;
    memblock_alloc(ctx, &mem_28979, bytes_28971, "mem_28979");
    
    int32_t tmp_offs_29261 = 0;
    
    memmove(mem_28979.mem + tmp_offs_29261 * 4, mem_28482.mem + 0, 0);
    tmp_offs_29261 = tmp_offs_29261;
    memmove(mem_28979.mem + tmp_offs_29261 * 4, mem_28952.mem + 0, x_27118 *
            sizeof(int32_t));
    tmp_offs_29261 += x_27118;
    
    bool loop_cond_27132 = slt32(1, x_27118);
    int32_t sizze_27133;
    int32_t sizze_27134;
    int32_t sizze_27135;
    int64_t res_mem_sizze_29022;
    struct memblock res_mem_29023;
    
    res_mem_29023.references = NULL;
    
    int64_t res_mem_sizze_29024;
    struct memblock res_mem_29025;
    
    res_mem_29025.references = NULL;
    
    int64_t res_mem_sizze_29026;
    struct memblock res_mem_29027;
    
    res_mem_29027.references = NULL;
    
    int32_t res_27139;
    
    if (empty_slice_27119) {
        struct memblock mem_28982;
        
        mem_28982.references = NULL;
        memblock_alloc(ctx, &mem_28982, bytes_28971, "mem_28982");
        memmove(mem_28982.mem + 0, mem_28973.mem + 0, x_27118 *
                sizeof(int32_t));
        
        struct memblock mem_28985;
        
        mem_28985.references = NULL;
        memblock_alloc(ctx, &mem_28985, bytes_28971, "mem_28985");
        memmove(mem_28985.mem + 0, mem_28976.mem + 0, x_27118 *
                sizeof(int32_t));
        
        struct memblock mem_28988;
        
        mem_28988.references = NULL;
        memblock_alloc(ctx, &mem_28988, bytes_28971, "mem_28988");
        memmove(mem_28988.mem + 0, mem_28979.mem + 0, x_27118 *
                sizeof(int32_t));
        sizze_27133 = x_27118;
        sizze_27134 = x_27118;
        sizze_27135 = x_27118;
        res_mem_sizze_29022 = bytes_28971;
        memblock_set(ctx, &res_mem_29023, &mem_28982, "mem_28982");
        res_mem_sizze_29024 = bytes_28971;
        memblock_set(ctx, &res_mem_29025, &mem_28985, "mem_28985");
        res_mem_sizze_29026 = bytes_28971;
        memblock_set(ctx, &res_mem_29027, &mem_28988, "mem_28988");
        res_27139 = 0;
        memblock_unref(ctx, &mem_28988, "mem_28988");
        memblock_unref(ctx, &mem_28985, "mem_28985");
        memblock_unref(ctx, &mem_28982, "mem_28982");
    } else {
        bool res_27151;
        int32_t res_27152;
        int32_t res_27153;
        bool loop_while_27154;
        int32_t r_27155;
        int32_t n_27156;
        
        loop_while_27154 = loop_cond_27132;
        r_27155 = 0;
        n_27156 = x_27118;
        while (loop_while_27154) {
            int32_t res_27157 = sdiv32(n_27156, 2);
            int32_t res_27158 = 1 + r_27155;
            bool loop_cond_27159 = slt32(1, res_27157);
            bool loop_while_tmp_29262 = loop_cond_27159;
            int32_t r_tmp_29263 = res_27158;
            int32_t n_tmp_29264;
            
            n_tmp_29264 = res_27157;
            loop_while_27154 = loop_while_tmp_29262;
            r_27155 = r_tmp_29263;
            n_27156 = n_tmp_29264;
        }
        res_27151 = loop_while_27154;
        res_27152 = r_27155;
        res_27153 = n_27156;
        
        int32_t y_27160 = 1 << res_27152;
        bool cond_27161 = x_27118 == y_27160;
        int32_t y_27162 = 1 + res_27152;
        int32_t x_27163 = 1 << y_27162;
        int32_t arg_27164 = x_27163 - x_27118;
        bool bounds_invalid_upwards_27165 = slt32(arg_27164, 0);
        int32_t conc_tmp_27166 = x_27118 + arg_27164;
        int32_t sizze_27167;
        
        if (cond_27161) {
            sizze_27167 = x_27118;
        } else {
            sizze_27167 = conc_tmp_27166;
        }
        
        int32_t res_27168;
        
        if (cond_27161) {
            res_27168 = res_27152;
        } else {
            res_27168 = y_27162;
        }
        
        int64_t binop_x_29008 = sext_i32_i64(conc_tmp_27166);
        int64_t bytes_29007 = 4 * binop_x_29008;
        int64_t res_mem_sizze_29016;
        struct memblock res_mem_29017;
        
        res_mem_29017.references = NULL;
        
        int64_t res_mem_sizze_29018;
        struct memblock res_mem_29019;
        
        res_mem_29019.references = NULL;
        
        int64_t res_mem_sizze_29020;
        struct memblock res_mem_29021;
        
        res_mem_29021.references = NULL;
        if (cond_27161) {
            struct memblock mem_28991;
            
            mem_28991.references = NULL;
            memblock_alloc(ctx, &mem_28991, bytes_28971, "mem_28991");
            memmove(mem_28991.mem + 0, mem_28973.mem + 0, x_27118 *
                    sizeof(int32_t));
            
            struct memblock mem_28994;
            
            mem_28994.references = NULL;
            memblock_alloc(ctx, &mem_28994, bytes_28971, "mem_28994");
            memmove(mem_28994.mem + 0, mem_28976.mem + 0, x_27118 *
                    sizeof(int32_t));
            
            struct memblock mem_28997;
            
            mem_28997.references = NULL;
            memblock_alloc(ctx, &mem_28997, bytes_28971, "mem_28997");
            memmove(mem_28997.mem + 0, mem_28979.mem + 0, x_27118 *
                    sizeof(int32_t));
            res_mem_sizze_29016 = bytes_28971;
            memblock_set(ctx, &res_mem_29017, &mem_28991, "mem_28991");
            res_mem_sizze_29018 = bytes_28971;
            memblock_set(ctx, &res_mem_29019, &mem_28994, "mem_28994");
            res_mem_sizze_29020 = bytes_28971;
            memblock_set(ctx, &res_mem_29021, &mem_28997, "mem_28997");
            memblock_unref(ctx, &mem_28997, "mem_28997");
            memblock_unref(ctx, &mem_28994, "mem_28994");
            memblock_unref(ctx, &mem_28991, "mem_28991");
        } else {
            bool y_27186 = slt32(0, x_27118);
            bool index_certs_27187;
            
            if (!y_27186) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:184:14-184:92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-45:36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-20:70",
                                       "Index [", 0,
                                       "] out of bounds for array of shape [",
                                       x_27118, "].");
                memblock_unref(ctx, &res_mem_29021, "res_mem_29021");
                memblock_unref(ctx, &res_mem_29019, "res_mem_29019");
                memblock_unref(ctx, &res_mem_29017, "res_mem_29017");
                memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
                memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
                memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
                memblock_unref(ctx, &mem_28979, "mem_28979");
                memblock_unref(ctx, &mem_28976, "mem_28976");
                memblock_unref(ctx, &mem_28973, "mem_28973");
                memblock_unref(ctx, &mem_28952, "mem_28952");
                memblock_unref(ctx, &mem_28949, "mem_28949");
                memblock_unref(ctx, &mem_28946, "mem_28946");
                memblock_unref(ctx, &mem_28935, "mem_28935");
                memblock_unref(ctx, &mem_28932, "mem_28932");
                memblock_unref(ctx, &mem_28927, "mem_28927");
                memblock_unref(ctx, &mem_28922, "mem_28922");
                memblock_unref(ctx, &mem_28890, "mem_28890");
                memblock_unref(ctx, &mem_28869, "mem_28869");
                memblock_unref(ctx, &mem_28866, "mem_28866");
                memblock_unref(ctx, &mem_28863, "mem_28863");
                memblock_unref(ctx, &mem_28856, "mem_28856");
                memblock_unref(ctx, &mem_28851, "mem_28851");
                memblock_unref(ctx, &mem_28846, "mem_28846");
                memblock_unref(ctx, &mem_28763, "mem_28763");
                memblock_unref(ctx, &mem_28742, "mem_28742");
                memblock_unref(ctx, &mem_28739, "mem_28739");
                memblock_unref(ctx, &mem_28736, "mem_28736");
                memblock_unref(ctx, &mem_28725, "mem_28725");
                memblock_unref(ctx, &mem_28722, "mem_28722");
                memblock_unref(ctx, &mem_28717, "mem_28717");
                memblock_unref(ctx, &mem_28712, "mem_28712");
                memblock_unref(ctx, &mem_28579, "mem_28579");
                memblock_unref(ctx, &mem_28576, "mem_28576");
                memblock_unref(ctx, &mem_28573, "mem_28573");
                memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
                memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
                memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
                memblock_unref(ctx, &mem_28542, "mem_28542");
                memblock_unref(ctx, &mem_28539, "mem_28539");
                memblock_unref(ctx, &mem_28536, "mem_28536");
                memblock_unref(ctx, &mem_28515, "mem_28515");
                memblock_unref(ctx, &mem_28512, "mem_28512");
                memblock_unref(ctx, &mem_28509, "mem_28509");
                memblock_unref(ctx, &mem_28498, "mem_28498");
                memblock_unref(ctx, &mem_28495, "mem_28495");
                memblock_unref(ctx, &mem_28490, "mem_28490");
                memblock_unref(ctx, &mem_28485, "mem_28485");
                memblock_unref(ctx, &mem_28482, "mem_28482");
                memblock_unref(ctx, &mem_28368, "mem_28368");
                memblock_unref(ctx, &mem_28365, "mem_28365");
                memblock_unref(ctx, &mem_28362, "mem_28362");
                memblock_unref(ctx, &mem_28359, "mem_28359");
                memblock_unref(ctx, &mem_28356, "mem_28356");
                memblock_unref(ctx, &mem_28335, "mem_28335");
                memblock_unref(ctx, &mem_28332, "mem_28332");
                memblock_unref(ctx, &mem_28329, "mem_28329");
                memblock_unref(ctx, &mem_28322, "mem_28322");
                memblock_unref(ctx, &mem_28317, "mem_28317");
                memblock_unref(ctx, &mem_28312, "mem_28312");
                memblock_unref(ctx, &mem_28309, "mem_28309");
                return 1;
            }
            
            int32_t index_concat_27188 = *(int32_t *) &mem_28946.mem[0];
            int32_t index_concat_27189 = *(int32_t *) &mem_28949.mem[0];
            int32_t index_concat_27190 = *(int32_t *) &mem_28952.mem[0];
            int32_t res_27191;
            int32_t res_27192;
            int32_t res_27193;
            int32_t redout_27981;
            int32_t redout_27982;
            int32_t redout_27983;
            
            redout_27981 = index_concat_27188;
            redout_27982 = index_concat_27189;
            redout_27983 = index_concat_27190;
            for (int32_t i_27984 = 0; i_27984 < x_27118; i_27984++) {
                int32_t index_concat_28276 =
                        *(int32_t *) &mem_28946.mem[i_27984 * 4];
                int32_t index_concat_28270 =
                        *(int32_t *) &mem_28949.mem[i_27984 * 4];
                int32_t index_concat_28264 =
                        *(int32_t *) &mem_28952.mem[i_27984 * 4];
                bool cond_27200 = redout_27981 == index_concat_28276;
                bool res_27201 = sle32(redout_27982, index_concat_28270);
                bool res_27202 = sle32(redout_27981, index_concat_28276);
                bool x_27203 = cond_27200 && res_27201;
                bool x_27204 = !cond_27200;
                bool y_27205 = res_27202 && x_27204;
                bool res_27206 = x_27203 || y_27205;
                int32_t res_27207;
                
                if (res_27206) {
                    res_27207 = index_concat_28276;
                } else {
                    res_27207 = redout_27981;
                }
                
                int32_t res_27208;
                
                if (res_27206) {
                    res_27208 = index_concat_28270;
                } else {
                    res_27208 = redout_27982;
                }
                
                int32_t res_27209;
                
                if (res_27206) {
                    res_27209 = index_concat_28264;
                } else {
                    res_27209 = redout_27983;
                }
                
                int32_t redout_tmp_29265 = res_27207;
                int32_t redout_tmp_29266 = res_27208;
                int32_t redout_tmp_29267;
                
                redout_tmp_29267 = res_27209;
                redout_27981 = redout_tmp_29265;
                redout_27982 = redout_tmp_29266;
                redout_27983 = redout_tmp_29267;
            }
            res_27191 = redout_27981;
            res_27192 = redout_27982;
            res_27193 = redout_27983;
            
            bool eq_x_zz_27213 = 0 == arg_27164;
            bool not_p_27214 = !bounds_invalid_upwards_27165;
            bool p_and_eq_x_y_27215 = eq_x_zz_27213 && not_p_27214;
            bool dim_zzero_27216 = bounds_invalid_upwards_27165 ||
                 p_and_eq_x_y_27215;
            bool both_empty_27217 = eq_x_zz_27213 && dim_zzero_27216;
            bool eq_x_y_27218 = arg_27164 == 0;
            bool p_and_eq_x_y_27219 = bounds_invalid_upwards_27165 &&
                 eq_x_y_27218;
            bool dim_match_27220 = not_p_27214 || p_and_eq_x_y_27219;
            bool empty_or_match_27221 = both_empty_27217 || dim_match_27220;
            bool empty_or_match_cert_27222;
            
            if (!empty_or_match_27221) {
                ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                       "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:184:14-184:92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-45:36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-21:57 -> /futlib/array.fut:66:1-67:19",
                                       "Function return value does not match shape of type ",
                                       "*", "[", arg_27164, "]", "t");
                memblock_unref(ctx, &res_mem_29021, "res_mem_29021");
                memblock_unref(ctx, &res_mem_29019, "res_mem_29019");
                memblock_unref(ctx, &res_mem_29017, "res_mem_29017");
                memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
                memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
                memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
                memblock_unref(ctx, &mem_28979, "mem_28979");
                memblock_unref(ctx, &mem_28976, "mem_28976");
                memblock_unref(ctx, &mem_28973, "mem_28973");
                memblock_unref(ctx, &mem_28952, "mem_28952");
                memblock_unref(ctx, &mem_28949, "mem_28949");
                memblock_unref(ctx, &mem_28946, "mem_28946");
                memblock_unref(ctx, &mem_28935, "mem_28935");
                memblock_unref(ctx, &mem_28932, "mem_28932");
                memblock_unref(ctx, &mem_28927, "mem_28927");
                memblock_unref(ctx, &mem_28922, "mem_28922");
                memblock_unref(ctx, &mem_28890, "mem_28890");
                memblock_unref(ctx, &mem_28869, "mem_28869");
                memblock_unref(ctx, &mem_28866, "mem_28866");
                memblock_unref(ctx, &mem_28863, "mem_28863");
                memblock_unref(ctx, &mem_28856, "mem_28856");
                memblock_unref(ctx, &mem_28851, "mem_28851");
                memblock_unref(ctx, &mem_28846, "mem_28846");
                memblock_unref(ctx, &mem_28763, "mem_28763");
                memblock_unref(ctx, &mem_28742, "mem_28742");
                memblock_unref(ctx, &mem_28739, "mem_28739");
                memblock_unref(ctx, &mem_28736, "mem_28736");
                memblock_unref(ctx, &mem_28725, "mem_28725");
                memblock_unref(ctx, &mem_28722, "mem_28722");
                memblock_unref(ctx, &mem_28717, "mem_28717");
                memblock_unref(ctx, &mem_28712, "mem_28712");
                memblock_unref(ctx, &mem_28579, "mem_28579");
                memblock_unref(ctx, &mem_28576, "mem_28576");
                memblock_unref(ctx, &mem_28573, "mem_28573");
                memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
                memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
                memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
                memblock_unref(ctx, &mem_28542, "mem_28542");
                memblock_unref(ctx, &mem_28539, "mem_28539");
                memblock_unref(ctx, &mem_28536, "mem_28536");
                memblock_unref(ctx, &mem_28515, "mem_28515");
                memblock_unref(ctx, &mem_28512, "mem_28512");
                memblock_unref(ctx, &mem_28509, "mem_28509");
                memblock_unref(ctx, &mem_28498, "mem_28498");
                memblock_unref(ctx, &mem_28495, "mem_28495");
                memblock_unref(ctx, &mem_28490, "mem_28490");
                memblock_unref(ctx, &mem_28485, "mem_28485");
                memblock_unref(ctx, &mem_28482, "mem_28482");
                memblock_unref(ctx, &mem_28368, "mem_28368");
                memblock_unref(ctx, &mem_28365, "mem_28365");
                memblock_unref(ctx, &mem_28362, "mem_28362");
                memblock_unref(ctx, &mem_28359, "mem_28359");
                memblock_unref(ctx, &mem_28356, "mem_28356");
                memblock_unref(ctx, &mem_28335, "mem_28335");
                memblock_unref(ctx, &mem_28332, "mem_28332");
                memblock_unref(ctx, &mem_28329, "mem_28329");
                memblock_unref(ctx, &mem_28322, "mem_28322");
                memblock_unref(ctx, &mem_28317, "mem_28317");
                memblock_unref(ctx, &mem_28312, "mem_28312");
                memblock_unref(ctx, &mem_28309, "mem_28309");
                return 1;
            }
            
            int64_t binop_x_28999 = sext_i32_i64(arg_27164);
            int64_t bytes_28998 = 4 * binop_x_28999;
            struct memblock mem_29000;
            
            mem_29000.references = NULL;
            memblock_alloc(ctx, &mem_29000, bytes_28998, "mem_29000");
            for (int32_t i_29268 = 0; i_29268 < arg_27164; i_29268++) {
                *(int32_t *) &mem_29000.mem[i_29268 * 4] = res_27191;
            }
            
            struct memblock mem_29003;
            
            mem_29003.references = NULL;
            memblock_alloc(ctx, &mem_29003, bytes_28998, "mem_29003");
            for (int32_t i_29269 = 0; i_29269 < arg_27164; i_29269++) {
                *(int32_t *) &mem_29003.mem[i_29269 * 4] = res_27192;
            }
            
            struct memblock mem_29006;
            
            mem_29006.references = NULL;
            memblock_alloc(ctx, &mem_29006, bytes_28998, "mem_29006");
            for (int32_t i_29270 = 0; i_29270 < arg_27164; i_29270++) {
                *(int32_t *) &mem_29006.mem[i_29270 * 4] = res_27193;
            }
            
            struct memblock mem_29009;
            
            mem_29009.references = NULL;
            memblock_alloc(ctx, &mem_29009, bytes_29007, "mem_29009");
            
            int32_t tmp_offs_29271 = 0;
            
            memmove(mem_29009.mem + tmp_offs_29271 * 4, mem_28482.mem + 0, 0);
            tmp_offs_29271 = tmp_offs_29271;
            memmove(mem_29009.mem + tmp_offs_29271 * 4, mem_28946.mem + 0,
                    x_27118 * sizeof(int32_t));
            tmp_offs_29271 += x_27118;
            memmove(mem_29009.mem + tmp_offs_29271 * 4, mem_29000.mem + 0,
                    arg_27164 * sizeof(int32_t));
            tmp_offs_29271 += arg_27164;
            memblock_unref(ctx, &mem_29000, "mem_29000");
            
            struct memblock mem_29012;
            
            mem_29012.references = NULL;
            memblock_alloc(ctx, &mem_29012, bytes_29007, "mem_29012");
            
            int32_t tmp_offs_29272 = 0;
            
            memmove(mem_29012.mem + tmp_offs_29272 * 4, mem_28482.mem + 0, 0);
            tmp_offs_29272 = tmp_offs_29272;
            memmove(mem_29012.mem + tmp_offs_29272 * 4, mem_28949.mem + 0,
                    x_27118 * sizeof(int32_t));
            tmp_offs_29272 += x_27118;
            memmove(mem_29012.mem + tmp_offs_29272 * 4, mem_29003.mem + 0,
                    arg_27164 * sizeof(int32_t));
            tmp_offs_29272 += arg_27164;
            memblock_unref(ctx, &mem_29003, "mem_29003");
            
            struct memblock mem_29015;
            
            mem_29015.references = NULL;
            memblock_alloc(ctx, &mem_29015, bytes_29007, "mem_29015");
            
            int32_t tmp_offs_29273 = 0;
            
            memmove(mem_29015.mem + tmp_offs_29273 * 4, mem_28482.mem + 0, 0);
            tmp_offs_29273 = tmp_offs_29273;
            memmove(mem_29015.mem + tmp_offs_29273 * 4, mem_28952.mem + 0,
                    x_27118 * sizeof(int32_t));
            tmp_offs_29273 += x_27118;
            memmove(mem_29015.mem + tmp_offs_29273 * 4, mem_29006.mem + 0,
                    arg_27164 * sizeof(int32_t));
            tmp_offs_29273 += arg_27164;
            memblock_unref(ctx, &mem_29006, "mem_29006");
            res_mem_sizze_29016 = bytes_29007;
            memblock_set(ctx, &res_mem_29017, &mem_29009, "mem_29009");
            res_mem_sizze_29018 = bytes_29007;
            memblock_set(ctx, &res_mem_29019, &mem_29012, "mem_29012");
            res_mem_sizze_29020 = bytes_29007;
            memblock_set(ctx, &res_mem_29021, &mem_29015, "mem_29015");
            memblock_unref(ctx, &mem_29015, "mem_29015");
            memblock_unref(ctx, &mem_29012, "mem_29012");
            memblock_unref(ctx, &mem_29009, "mem_29009");
            memblock_unref(ctx, &mem_29006, "mem_29006");
            memblock_unref(ctx, &mem_29003, "mem_29003");
            memblock_unref(ctx, &mem_29000, "mem_29000");
        }
        sizze_27133 = sizze_27167;
        sizze_27134 = sizze_27167;
        sizze_27135 = sizze_27167;
        res_mem_sizze_29022 = res_mem_sizze_29016;
        memblock_set(ctx, &res_mem_29023, &res_mem_29017, "res_mem_29017");
        res_mem_sizze_29024 = res_mem_sizze_29018;
        memblock_set(ctx, &res_mem_29025, &res_mem_29019, "res_mem_29019");
        res_mem_sizze_29026 = res_mem_sizze_29020;
        memblock_set(ctx, &res_mem_29027, &res_mem_29021, "res_mem_29021");
        res_27139 = res_27168;
        memblock_unref(ctx, &res_mem_29021, "res_mem_29021");
        memblock_unref(ctx, &res_mem_29019, "res_mem_29019");
        memblock_unref(ctx, &res_mem_29017, "res_mem_29017");
    }
    memblock_unref(ctx, &mem_28482, "mem_28482");
    memblock_unref(ctx, &mem_28946, "mem_28946");
    memblock_unref(ctx, &mem_28949, "mem_28949");
    memblock_unref(ctx, &mem_28952, "mem_28952");
    memblock_unref(ctx, &mem_28973, "mem_28973");
    memblock_unref(ctx, &mem_28976, "mem_28976");
    memblock_unref(ctx, &mem_28979, "mem_28979");
    
    bool dim_zzero_27232 = 0 == sizze_27134;
    bool dim_zzero_27233 = 0 == sizze_27133;
    bool both_empty_27234 = dim_zzero_27232 && dim_zzero_27233;
    bool dim_match_27235 = sizze_27133 == sizze_27134;
    bool empty_or_match_27236 = both_empty_27234 || dim_match_27235;
    bool empty_or_match_cert_27237;
    
    if (!empty_or_match_27236) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:184:14-184:92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-45:36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
        memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
        memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
        memblock_unref(ctx, &mem_28979, "mem_28979");
        memblock_unref(ctx, &mem_28976, "mem_28976");
        memblock_unref(ctx, &mem_28973, "mem_28973");
        memblock_unref(ctx, &mem_28952, "mem_28952");
        memblock_unref(ctx, &mem_28949, "mem_28949");
        memblock_unref(ctx, &mem_28946, "mem_28946");
        memblock_unref(ctx, &mem_28935, "mem_28935");
        memblock_unref(ctx, &mem_28932, "mem_28932");
        memblock_unref(ctx, &mem_28927, "mem_28927");
        memblock_unref(ctx, &mem_28922, "mem_28922");
        memblock_unref(ctx, &mem_28890, "mem_28890");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    bool dim_zzero_27239 = 0 == sizze_27135;
    bool both_empty_27240 = dim_zzero_27233 && dim_zzero_27239;
    bool dim_match_27241 = sizze_27133 == sizze_27135;
    bool empty_or_match_27242 = both_empty_27240 || dim_match_27241;
    bool empty_or_match_cert_27243;
    
    if (!empty_or_match_27242) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:184:14-184:92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-45:36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
        memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
        memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
        memblock_unref(ctx, &mem_28979, "mem_28979");
        memblock_unref(ctx, &mem_28976, "mem_28976");
        memblock_unref(ctx, &mem_28973, "mem_28973");
        memblock_unref(ctx, &mem_28952, "mem_28952");
        memblock_unref(ctx, &mem_28949, "mem_28949");
        memblock_unref(ctx, &mem_28946, "mem_28946");
        memblock_unref(ctx, &mem_28935, "mem_28935");
        memblock_unref(ctx, &mem_28932, "mem_28932");
        memblock_unref(ctx, &mem_28927, "mem_28927");
        memblock_unref(ctx, &mem_28922, "mem_28922");
        memblock_unref(ctx, &mem_28890, "mem_28890");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    int64_t binop_x_29041 = sext_i32_i64(sizze_27133);
    int64_t bytes_29040 = 4 * binop_x_29041;
    int64_t indexed_mem_sizze_29067;
    struct memblock indexed_mem_29068;
    
    indexed_mem_29068.references = NULL;
    
    int64_t indexed_mem_sizze_29069;
    struct memblock indexed_mem_29070;
    
    indexed_mem_29070.references = NULL;
    
    int64_t indexed_mem_sizze_29071;
    struct memblock indexed_mem_29072;
    
    indexed_mem_29072.references = NULL;
    
    int64_t xs_mem_sizze_29028;
    struct memblock xs_mem_29029;
    
    xs_mem_29029.references = NULL;
    
    int64_t xs_mem_sizze_29030;
    struct memblock xs_mem_29031;
    
    xs_mem_29031.references = NULL;
    
    int64_t xs_mem_sizze_29032;
    struct memblock xs_mem_29033;
    
    xs_mem_29033.references = NULL;
    xs_mem_sizze_29028 = res_mem_sizze_29022;
    memblock_set(ctx, &xs_mem_29029, &res_mem_29023, "res_mem_29023");
    xs_mem_sizze_29030 = res_mem_sizze_29024;
    memblock_set(ctx, &xs_mem_29031, &res_mem_29025, "res_mem_29025");
    xs_mem_sizze_29032 = res_mem_sizze_29026;
    memblock_set(ctx, &xs_mem_29033, &res_mem_29027, "res_mem_29027");
    for (int32_t i_27252 = 0; i_27252 < res_27139; i_27252++) {
        int32_t upper_bound_27253 = 1 + i_27252;
        int64_t res_mem_sizze_29061;
        struct memblock res_mem_29062;
        
        res_mem_29062.references = NULL;
        
        int64_t res_mem_sizze_29063;
        struct memblock res_mem_29064;
        
        res_mem_29064.references = NULL;
        
        int64_t res_mem_sizze_29065;
        struct memblock res_mem_29066;
        
        res_mem_29066.references = NULL;
        
        int64_t xs_mem_sizze_29034;
        struct memblock xs_mem_29035;
        
        xs_mem_29035.references = NULL;
        
        int64_t xs_mem_sizze_29036;
        struct memblock xs_mem_29037;
        
        xs_mem_29037.references = NULL;
        
        int64_t xs_mem_sizze_29038;
        struct memblock xs_mem_29039;
        
        xs_mem_29039.references = NULL;
        xs_mem_sizze_29034 = xs_mem_sizze_29028;
        memblock_set(ctx, &xs_mem_29035, &xs_mem_29029, "xs_mem_29029");
        xs_mem_sizze_29036 = xs_mem_sizze_29030;
        memblock_set(ctx, &xs_mem_29037, &xs_mem_29031, "xs_mem_29031");
        xs_mem_sizze_29038 = xs_mem_sizze_29032;
        memblock_set(ctx, &xs_mem_29039, &xs_mem_29033, "xs_mem_29033");
        for (int32_t j_27260 = 0; j_27260 < upper_bound_27253; j_27260++) {
            int32_t y_27261 = i_27252 - j_27260;
            int32_t res_27262 = 1 << y_27261;
            struct memblock mem_29042;
            
            mem_29042.references = NULL;
            memblock_alloc(ctx, &mem_29042, bytes_29040, "mem_29042");
            
            struct memblock mem_29045;
            
            mem_29045.references = NULL;
            memblock_alloc(ctx, &mem_29045, bytes_29040, "mem_29045");
            
            struct memblock mem_29048;
            
            mem_29048.references = NULL;
            memblock_alloc(ctx, &mem_29048, bytes_29040, "mem_29048");
            for (int32_t i_27991 = 0; i_27991 < sizze_27133; i_27991++) {
                int32_t res_27267 = *(int32_t *) &xs_mem_29035.mem[i_27991 * 4];
                int32_t res_27268 = *(int32_t *) &xs_mem_29037.mem[i_27991 * 4];
                int32_t res_27269 = *(int32_t *) &xs_mem_29039.mem[i_27991 * 4];
                int32_t x_27270 = ashr32(i_27991, i_27252);
                int32_t x_27271 = 2 & x_27270;
                bool res_27272 = x_27271 == 0;
                int32_t x_27273 = res_27262 & i_27991;
                bool cond_27274 = x_27273 == 0;
                int32_t res_27275;
                int32_t res_27276;
                int32_t res_27277;
                
                if (cond_27274) {
                    int32_t i_27278 = res_27262 | i_27991;
                    int32_t res_27279 = *(int32_t *) &xs_mem_29035.mem[i_27278 *
                                                                       4];
                    int32_t res_27280 = *(int32_t *) &xs_mem_29037.mem[i_27278 *
                                                                       4];
                    int32_t res_27281 = *(int32_t *) &xs_mem_29039.mem[i_27278 *
                                                                       4];
                    bool cond_27282 = res_27279 == res_27267;
                    bool res_27283 = sle32(res_27280, res_27268);
                    bool res_27284 = sle32(res_27279, res_27267);
                    bool x_27285 = cond_27282 && res_27283;
                    bool x_27286 = !cond_27282;
                    bool y_27287 = res_27284 && x_27286;
                    bool res_27288 = x_27285 || y_27287;
                    bool cond_27289 = res_27288 == res_27272;
                    int32_t res_27290;
                    
                    if (cond_27289) {
                        res_27290 = res_27279;
                    } else {
                        res_27290 = res_27267;
                    }
                    
                    int32_t res_27291;
                    
                    if (cond_27289) {
                        res_27291 = res_27280;
                    } else {
                        res_27291 = res_27268;
                    }
                    
                    int32_t res_27292;
                    
                    if (cond_27289) {
                        res_27292 = res_27281;
                    } else {
                        res_27292 = res_27269;
                    }
                    res_27275 = res_27290;
                    res_27276 = res_27291;
                    res_27277 = res_27292;
                } else {
                    int32_t i_27293 = res_27262 ^ i_27991;
                    int32_t res_27294 = *(int32_t *) &xs_mem_29035.mem[i_27293 *
                                                                       4];
                    int32_t res_27295 = *(int32_t *) &xs_mem_29037.mem[i_27293 *
                                                                       4];
                    int32_t res_27296 = *(int32_t *) &xs_mem_29039.mem[i_27293 *
                                                                       4];
                    bool cond_27297 = res_27267 == res_27294;
                    bool res_27298 = sle32(res_27268, res_27295);
                    bool res_27299 = sle32(res_27267, res_27294);
                    bool x_27300 = cond_27297 && res_27298;
                    bool x_27301 = !cond_27297;
                    bool y_27302 = res_27299 && x_27301;
                    bool res_27303 = x_27300 || y_27302;
                    bool cond_27304 = res_27303 == res_27272;
                    int32_t res_27305;
                    
                    if (cond_27304) {
                        res_27305 = res_27294;
                    } else {
                        res_27305 = res_27267;
                    }
                    
                    int32_t res_27306;
                    
                    if (cond_27304) {
                        res_27306 = res_27295;
                    } else {
                        res_27306 = res_27268;
                    }
                    
                    int32_t res_27307;
                    
                    if (cond_27304) {
                        res_27307 = res_27296;
                    } else {
                        res_27307 = res_27269;
                    }
                    res_27275 = res_27305;
                    res_27276 = res_27306;
                    res_27277 = res_27307;
                }
                *(int32_t *) &mem_29042.mem[i_27991 * 4] = res_27275;
                *(int32_t *) &mem_29045.mem[i_27991 * 4] = res_27276;
                *(int32_t *) &mem_29048.mem[i_27991 * 4] = res_27277;
            }
            
            int64_t xs_mem_sizze_tmp_29283 = bytes_29040;
            struct memblock xs_mem_tmp_29284;
            
            xs_mem_tmp_29284.references = NULL;
            memblock_set(ctx, &xs_mem_tmp_29284, &mem_29042, "mem_29042");
            
            int64_t xs_mem_sizze_tmp_29285 = bytes_29040;
            struct memblock xs_mem_tmp_29286;
            
            xs_mem_tmp_29286.references = NULL;
            memblock_set(ctx, &xs_mem_tmp_29286, &mem_29045, "mem_29045");
            
            int64_t xs_mem_sizze_tmp_29287 = bytes_29040;
            struct memblock xs_mem_tmp_29288;
            
            xs_mem_tmp_29288.references = NULL;
            memblock_set(ctx, &xs_mem_tmp_29288, &mem_29048, "mem_29048");
            xs_mem_sizze_29034 = xs_mem_sizze_tmp_29283;
            memblock_set(ctx, &xs_mem_29035, &xs_mem_tmp_29284,
                         "xs_mem_tmp_29284");
            xs_mem_sizze_29036 = xs_mem_sizze_tmp_29285;
            memblock_set(ctx, &xs_mem_29037, &xs_mem_tmp_29286,
                         "xs_mem_tmp_29286");
            xs_mem_sizze_29038 = xs_mem_sizze_tmp_29287;
            memblock_set(ctx, &xs_mem_29039, &xs_mem_tmp_29288,
                         "xs_mem_tmp_29288");
            memblock_unref(ctx, &xs_mem_tmp_29288, "xs_mem_tmp_29288");
            memblock_unref(ctx, &xs_mem_tmp_29286, "xs_mem_tmp_29286");
            memblock_unref(ctx, &xs_mem_tmp_29284, "xs_mem_tmp_29284");
            memblock_unref(ctx, &mem_29048, "mem_29048");
            memblock_unref(ctx, &mem_29045, "mem_29045");
            memblock_unref(ctx, &mem_29042, "mem_29042");
        }
        res_mem_sizze_29061 = xs_mem_sizze_29034;
        memblock_set(ctx, &res_mem_29062, &xs_mem_29035, "xs_mem_29035");
        res_mem_sizze_29063 = xs_mem_sizze_29036;
        memblock_set(ctx, &res_mem_29064, &xs_mem_29037, "xs_mem_29037");
        res_mem_sizze_29065 = xs_mem_sizze_29038;
        memblock_set(ctx, &res_mem_29066, &xs_mem_29039, "xs_mem_29039");
        
        int64_t xs_mem_sizze_tmp_29274 = res_mem_sizze_29061;
        struct memblock xs_mem_tmp_29275;
        
        xs_mem_tmp_29275.references = NULL;
        memblock_set(ctx, &xs_mem_tmp_29275, &res_mem_29062, "res_mem_29062");
        
        int64_t xs_mem_sizze_tmp_29276 = res_mem_sizze_29063;
        struct memblock xs_mem_tmp_29277;
        
        xs_mem_tmp_29277.references = NULL;
        memblock_set(ctx, &xs_mem_tmp_29277, &res_mem_29064, "res_mem_29064");
        
        int64_t xs_mem_sizze_tmp_29278 = res_mem_sizze_29065;
        struct memblock xs_mem_tmp_29279;
        
        xs_mem_tmp_29279.references = NULL;
        memblock_set(ctx, &xs_mem_tmp_29279, &res_mem_29066, "res_mem_29066");
        xs_mem_sizze_29028 = xs_mem_sizze_tmp_29274;
        memblock_set(ctx, &xs_mem_29029, &xs_mem_tmp_29275, "xs_mem_tmp_29275");
        xs_mem_sizze_29030 = xs_mem_sizze_tmp_29276;
        memblock_set(ctx, &xs_mem_29031, &xs_mem_tmp_29277, "xs_mem_tmp_29277");
        xs_mem_sizze_29032 = xs_mem_sizze_tmp_29278;
        memblock_set(ctx, &xs_mem_29033, &xs_mem_tmp_29279, "xs_mem_tmp_29279");
        memblock_unref(ctx, &xs_mem_tmp_29279, "xs_mem_tmp_29279");
        memblock_unref(ctx, &xs_mem_tmp_29277, "xs_mem_tmp_29277");
        memblock_unref(ctx, &xs_mem_tmp_29275, "xs_mem_tmp_29275");
        memblock_unref(ctx, &xs_mem_29039, "xs_mem_29039");
        memblock_unref(ctx, &xs_mem_29037, "xs_mem_29037");
        memblock_unref(ctx, &xs_mem_29035, "xs_mem_29035");
        memblock_unref(ctx, &res_mem_29066, "res_mem_29066");
        memblock_unref(ctx, &res_mem_29064, "res_mem_29064");
        memblock_unref(ctx, &res_mem_29062, "res_mem_29062");
    }
    indexed_mem_sizze_29067 = xs_mem_sizze_29028;
    memblock_set(ctx, &indexed_mem_29068, &xs_mem_29029, "xs_mem_29029");
    indexed_mem_sizze_29069 = xs_mem_sizze_29030;
    memblock_set(ctx, &indexed_mem_29070, &xs_mem_29031, "xs_mem_29031");
    indexed_mem_sizze_29071 = xs_mem_sizze_29032;
    memblock_set(ctx, &indexed_mem_29072, &xs_mem_29033, "xs_mem_29033");
    memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
    memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
    memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
    
    int32_t x_27308 = abs(x_27118);
    bool empty_slice_27309 = x_27308 == 0;
    int32_t m_27310 = x_27308 - 1;
    bool zzero_leq_i_p_m_t_s_27311 = sle32(0, m_27310);
    bool i_p_m_t_s_leq_w_27312 = slt32(m_27310, sizze_27133);
    bool y_27313 = zzero_leq_i_p_m_t_s_27311 && i_p_m_t_s_leq_w_27312;
    bool ok_or_empty_27314 = empty_slice_27309 || y_27313;
    bool index_certs_27315;
    
    if (!ok_or_empty_27314) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:184:14-184:92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                               "Index [", "", ":", x_27118,
                               "] out of bounds for array of shape [",
                               sizze_27133, "].");
        memblock_unref(ctx, &xs_mem_29033, "xs_mem_29033");
        memblock_unref(ctx, &xs_mem_29031, "xs_mem_29031");
        memblock_unref(ctx, &xs_mem_29029, "xs_mem_29029");
        memblock_unref(ctx, &indexed_mem_29072, "indexed_mem_29072");
        memblock_unref(ctx, &indexed_mem_29070, "indexed_mem_29070");
        memblock_unref(ctx, &indexed_mem_29068, "indexed_mem_29068");
        memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
        memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
        memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
        memblock_unref(ctx, &mem_28979, "mem_28979");
        memblock_unref(ctx, &mem_28976, "mem_28976");
        memblock_unref(ctx, &mem_28973, "mem_28973");
        memblock_unref(ctx, &mem_28952, "mem_28952");
        memblock_unref(ctx, &mem_28949, "mem_28949");
        memblock_unref(ctx, &mem_28946, "mem_28946");
        memblock_unref(ctx, &mem_28935, "mem_28935");
        memblock_unref(ctx, &mem_28932, "mem_28932");
        memblock_unref(ctx, &mem_28927, "mem_28927");
        memblock_unref(ctx, &mem_28922, "mem_28922");
        memblock_unref(ctx, &mem_28890, "mem_28890");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    int64_t binop_x_29074 = sext_i32_i64(x_27308);
    int64_t bytes_29073 = 4 * binop_x_29074;
    struct memblock mem_29075;
    
    mem_29075.references = NULL;
    memblock_alloc(ctx, &mem_29075, bytes_29073, "mem_29075");
    
    struct memblock mem_29077;
    
    mem_29077.references = NULL;
    memblock_alloc(ctx, &mem_29077, binop_x_29074, "mem_29077");
    
    int32_t discard_28018;
    int32_t scanacc_28003 = 0;
    
    for (int32_t i_28009 = 0; i_28009 < x_27308; i_28009++) {
        int32_t x_27337 = *(int32_t *) &indexed_mem_29068.mem[i_28009 * 4];
        int32_t x_27338 = *(int32_t *) &indexed_mem_29070.mem[i_28009 * 4];
        int32_t i_p_o_28286 = -1 + i_28009;
        int32_t rot_i_28287 = smod32(i_p_o_28286, x_27308);
        int32_t x_27339 = *(int32_t *) &indexed_mem_29068.mem[rot_i_28287 * 4];
        int32_t x_27340 = *(int32_t *) &indexed_mem_29070.mem[rot_i_28287 * 4];
        int32_t x_27341 = *(int32_t *) &indexed_mem_29072.mem[i_28009 * 4];
        bool res_27342 = x_27337 == x_27339;
        bool res_27343 = x_27338 == x_27340;
        bool eq_27344 = res_27342 && res_27343;
        bool res_27345 = !eq_27344;
        int32_t res_27335;
        
        if (res_27345) {
            res_27335 = x_27341;
        } else {
            int32_t res_27336 = x_27341 + scanacc_28003;
            
            res_27335 = res_27336;
        }
        *(int32_t *) &mem_29075.mem[i_28009 * 4] = res_27335;
        *(bool *) &mem_29077.mem[i_28009] = res_27345;
        
        int32_t scanacc_tmp_29295 = res_27335;
        
        scanacc_28003 = scanacc_tmp_29295;
    }
    discard_28018 = scanacc_28003;
    memblock_unref(ctx, &indexed_mem_29068, "indexed_mem_29068");
    memblock_unref(ctx, &indexed_mem_29070, "indexed_mem_29070");
    memblock_unref(ctx, &indexed_mem_29072, "indexed_mem_29072");
    
    struct memblock mem_29088;
    
    mem_29088.references = NULL;
    memblock_alloc(ctx, &mem_29088, bytes_29073, "mem_29088");
    
    int32_t discard_28024;
    int32_t scanacc_28020 = 0;
    
    for (int32_t i_28022 = 0; i_28022 < x_27308; i_28022++) {
        int32_t i_p_o_28294 = 1 + i_28022;
        int32_t rot_i_28295 = smod32(i_p_o_28294, x_27308);
        bool x_27351 = *(bool *) &mem_29077.mem[rot_i_28295];
        int32_t res_27352;
        
        if (x_27351) {
            res_27352 = 1;
        } else {
            res_27352 = 0;
        }
        
        int32_t res_27350 = res_27352 + scanacc_28020;
        
        *(int32_t *) &mem_29088.mem[i_28022 * 4] = res_27350;
        
        int32_t scanacc_tmp_29298 = res_27350;
        
        scanacc_28020 = scanacc_tmp_29298;
    }
    discard_28024 = scanacc_28020;
    
    bool cond_27353 = slt32(1, x_27308);
    int32_t res_27354;
    
    if (cond_27353) {
        bool index_certs_27355;
        
        if (!zzero_leq_i_p_m_t_s_27311) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:186:13-186:86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-29:59",
                                   "Index [", m_27310,
                                   "] out of bounds for array of shape [",
                                   x_27308, "].");
            memblock_unref(ctx, &mem_29088, "mem_29088");
            memblock_unref(ctx, &mem_29077, "mem_29077");
            memblock_unref(ctx, &mem_29075, "mem_29075");
            memblock_unref(ctx, &xs_mem_29033, "xs_mem_29033");
            memblock_unref(ctx, &xs_mem_29031, "xs_mem_29031");
            memblock_unref(ctx, &xs_mem_29029, "xs_mem_29029");
            memblock_unref(ctx, &indexed_mem_29072, "indexed_mem_29072");
            memblock_unref(ctx, &indexed_mem_29070, "indexed_mem_29070");
            memblock_unref(ctx, &indexed_mem_29068, "indexed_mem_29068");
            memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
            memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
            memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
            memblock_unref(ctx, &mem_28979, "mem_28979");
            memblock_unref(ctx, &mem_28976, "mem_28976");
            memblock_unref(ctx, &mem_28973, "mem_28973");
            memblock_unref(ctx, &mem_28952, "mem_28952");
            memblock_unref(ctx, &mem_28949, "mem_28949");
            memblock_unref(ctx, &mem_28946, "mem_28946");
            memblock_unref(ctx, &mem_28935, "mem_28935");
            memblock_unref(ctx, &mem_28932, "mem_28932");
            memblock_unref(ctx, &mem_28927, "mem_28927");
            memblock_unref(ctx, &mem_28922, "mem_28922");
            memblock_unref(ctx, &mem_28890, "mem_28890");
            memblock_unref(ctx, &mem_28869, "mem_28869");
            memblock_unref(ctx, &mem_28866, "mem_28866");
            memblock_unref(ctx, &mem_28863, "mem_28863");
            memblock_unref(ctx, &mem_28856, "mem_28856");
            memblock_unref(ctx, &mem_28851, "mem_28851");
            memblock_unref(ctx, &mem_28846, "mem_28846");
            memblock_unref(ctx, &mem_28763, "mem_28763");
            memblock_unref(ctx, &mem_28742, "mem_28742");
            memblock_unref(ctx, &mem_28739, "mem_28739");
            memblock_unref(ctx, &mem_28736, "mem_28736");
            memblock_unref(ctx, &mem_28725, "mem_28725");
            memblock_unref(ctx, &mem_28722, "mem_28722");
            memblock_unref(ctx, &mem_28717, "mem_28717");
            memblock_unref(ctx, &mem_28712, "mem_28712");
            memblock_unref(ctx, &mem_28579, "mem_28579");
            memblock_unref(ctx, &mem_28576, "mem_28576");
            memblock_unref(ctx, &mem_28573, "mem_28573");
            memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
            memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
            memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
            memblock_unref(ctx, &mem_28542, "mem_28542");
            memblock_unref(ctx, &mem_28539, "mem_28539");
            memblock_unref(ctx, &mem_28536, "mem_28536");
            memblock_unref(ctx, &mem_28515, "mem_28515");
            memblock_unref(ctx, &mem_28512, "mem_28512");
            memblock_unref(ctx, &mem_28509, "mem_28509");
            memblock_unref(ctx, &mem_28498, "mem_28498");
            memblock_unref(ctx, &mem_28495, "mem_28495");
            memblock_unref(ctx, &mem_28490, "mem_28490");
            memblock_unref(ctx, &mem_28485, "mem_28485");
            memblock_unref(ctx, &mem_28482, "mem_28482");
            memblock_unref(ctx, &mem_28368, "mem_28368");
            memblock_unref(ctx, &mem_28365, "mem_28365");
            memblock_unref(ctx, &mem_28362, "mem_28362");
            memblock_unref(ctx, &mem_28359, "mem_28359");
            memblock_unref(ctx, &mem_28356, "mem_28356");
            memblock_unref(ctx, &mem_28335, "mem_28335");
            memblock_unref(ctx, &mem_28332, "mem_28332");
            memblock_unref(ctx, &mem_28329, "mem_28329");
            memblock_unref(ctx, &mem_28322, "mem_28322");
            memblock_unref(ctx, &mem_28317, "mem_28317");
            memblock_unref(ctx, &mem_28312, "mem_28312");
            memblock_unref(ctx, &mem_28309, "mem_28309");
            return 1;
        }
        
        int32_t res_27356 = *(int32_t *) &mem_29088.mem[m_27310 * 4];
        
        res_27354 = res_27356;
    } else {
        res_27354 = 0;
    }
    
    bool bounds_invalid_upwards_27357 = slt32(res_27354, 0);
    bool eq_x_zz_27358 = 0 == res_27354;
    bool not_p_27359 = !bounds_invalid_upwards_27357;
    bool p_and_eq_x_y_27360 = eq_x_zz_27358 && not_p_27359;
    bool dim_zzero_27361 = bounds_invalid_upwards_27357 || p_and_eq_x_y_27360;
    bool both_empty_27362 = eq_x_zz_27358 && dim_zzero_27361;
    bool empty_or_match_27363 = not_p_27359 || both_empty_27362;
    bool empty_or_match_cert_27364;
    
    if (!empty_or_match_27363) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:186:13-186:86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-33:41 -> /futlib/array.fut:66:1-67:19",
                               "Function return value does not match shape of type ",
                               "*", "[", res_27354, "]", "t");
        memblock_unref(ctx, &mem_29088, "mem_29088");
        memblock_unref(ctx, &mem_29077, "mem_29077");
        memblock_unref(ctx, &mem_29075, "mem_29075");
        memblock_unref(ctx, &xs_mem_29033, "xs_mem_29033");
        memblock_unref(ctx, &xs_mem_29031, "xs_mem_29031");
        memblock_unref(ctx, &xs_mem_29029, "xs_mem_29029");
        memblock_unref(ctx, &indexed_mem_29072, "indexed_mem_29072");
        memblock_unref(ctx, &indexed_mem_29070, "indexed_mem_29070");
        memblock_unref(ctx, &indexed_mem_29068, "indexed_mem_29068");
        memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
        memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
        memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
        memblock_unref(ctx, &mem_28979, "mem_28979");
        memblock_unref(ctx, &mem_28976, "mem_28976");
        memblock_unref(ctx, &mem_28973, "mem_28973");
        memblock_unref(ctx, &mem_28952, "mem_28952");
        memblock_unref(ctx, &mem_28949, "mem_28949");
        memblock_unref(ctx, &mem_28946, "mem_28946");
        memblock_unref(ctx, &mem_28935, "mem_28935");
        memblock_unref(ctx, &mem_28932, "mem_28932");
        memblock_unref(ctx, &mem_28927, "mem_28927");
        memblock_unref(ctx, &mem_28922, "mem_28922");
        memblock_unref(ctx, &mem_28890, "mem_28890");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    int64_t binop_x_29094 = sext_i32_i64(res_27354);
    int64_t bytes_29093 = 4 * binop_x_29094;
    struct memblock mem_29095;
    
    mem_29095.references = NULL;
    memblock_alloc(ctx, &mem_29095, bytes_29093, "mem_29095");
    for (int32_t i_29300 = 0; i_29300 < res_27354; i_29300++) {
        *(int32_t *) &mem_29095.mem[i_29300 * 4] = 0;
    }
    for (int32_t write_iter_28025 = 0; write_iter_28025 < x_27308;
         write_iter_28025++) {
        int32_t write_iv_28029 = *(int32_t *) &mem_29088.mem[write_iter_28025 *
                                                             4];
        int32_t i_p_o_28296 = 1 + write_iter_28025;
        int32_t rot_i_28297 = smod32(i_p_o_28296, x_27308);
        bool write_iv_28030 = *(bool *) &mem_29077.mem[rot_i_28297];
        int32_t write_iv_28033 = *(int32_t *) &mem_29075.mem[write_iter_28025 *
                                                             4];
        int32_t res_27376;
        
        if (write_iv_28030) {
            int32_t res_27377 = write_iv_28029 - 1;
            
            res_27376 = res_27377;
        } else {
            res_27376 = -1;
        }
        
        bool less_than_zzero_28046 = slt32(res_27376, 0);
        bool greater_than_sizze_28047 = sle32(res_27354, res_27376);
        bool outside_bounds_dim_28048 = less_than_zzero_28046 ||
             greater_than_sizze_28047;
        
        if (!outside_bounds_dim_28048) {
            *(int32_t *) &mem_29095.mem[res_27376 * 4] = write_iv_28033;
        }
    }
    memblock_unref(ctx, &mem_29075, "mem_29075");
    memblock_unref(ctx, &mem_29077, "mem_29077");
    memblock_unref(ctx, &mem_29088, "mem_29088");
    
    struct memblock mem_29104;
    
    mem_29104.references = NULL;
    memblock_alloc(ctx, &mem_29104, bytes_29093, "mem_29104");
    
    struct memblock mem_29107;
    
    mem_29107.references = NULL;
    memblock_alloc(ctx, &mem_29107, bytes_29093, "mem_29107");
    
    int32_t discard_28060;
    int32_t scanacc_28054 = 0;
    
    for (int32_t i_28057 = 0; i_28057 < res_27354; i_28057++) {
        int32_t x_27383 = *(int32_t *) &mem_29095.mem[i_28057 * 4];
        bool not_arg_27384 = x_27383 == 0;
        bool res_27385 = !not_arg_27384;
        int32_t part_res_27386;
        
        if (res_27385) {
            part_res_27386 = 0;
        } else {
            part_res_27386 = 1;
        }
        
        int32_t part_res_27387;
        
        if (res_27385) {
            part_res_27387 = 1;
        } else {
            part_res_27387 = 0;
        }
        
        int32_t zz_27382 = part_res_27387 + scanacc_28054;
        
        *(int32_t *) &mem_29104.mem[i_28057 * 4] = zz_27382;
        *(int32_t *) &mem_29107.mem[i_28057 * 4] = part_res_27386;
        
        int32_t scanacc_tmp_29302 = zz_27382;
        
        scanacc_28054 = scanacc_tmp_29302;
    }
    discard_28060 = scanacc_28054;
    
    int32_t last_index_27388 = res_27354 - 1;
    bool is_empty_27389 = res_27354 == 0;
    int32_t partition_sizze_27390;
    
    if (is_empty_27389) {
        partition_sizze_27390 = 0;
    } else {
        int32_t last_offset_27391 =
                *(int32_t *) &mem_29104.mem[last_index_27388 * 4];
        
        partition_sizze_27390 = last_offset_27391;
    }
    
    int64_t binop_x_29117 = sext_i32_i64(partition_sizze_27390);
    int64_t bytes_29116 = 4 * binop_x_29117;
    struct memblock mem_29118;
    
    mem_29118.references = NULL;
    memblock_alloc(ctx, &mem_29118, bytes_29116, "mem_29118");
    for (int32_t write_iter_28061 = 0; write_iter_28061 < res_27354;
         write_iter_28061++) {
        int32_t write_iv_28063 = *(int32_t *) &mem_29107.mem[write_iter_28061 *
                                                             4];
        int32_t write_iv_28064 = *(int32_t *) &mem_29104.mem[write_iter_28061 *
                                                             4];
        int32_t write_iv_28067 = *(int32_t *) &mem_29095.mem[write_iter_28061 *
                                                             4];
        bool is_this_one_27399 = write_iv_28063 == 0;
        int32_t this_offset_27400 = -1 + write_iv_28064;
        int32_t total_res_27401;
        
        if (is_this_one_27399) {
            total_res_27401 = this_offset_27400;
        } else {
            total_res_27401 = -1;
        }
        
        bool less_than_zzero_28068 = slt32(total_res_27401, 0);
        bool greater_than_sizze_28069 = sle32(partition_sizze_27390,
                                              total_res_27401);
        bool outside_bounds_dim_28070 = less_than_zzero_28068 ||
             greater_than_sizze_28069;
        
        if (!outside_bounds_dim_28070) {
            *(int32_t *) &mem_29118.mem[total_res_27401 * 4] = write_iv_28067;
        }
    }
    memblock_unref(ctx, &mem_29095, "mem_29095");
    memblock_unref(ctx, &mem_29104, "mem_29104");
    memblock_unref(ctx, &mem_29107, "mem_29107");
    
    int32_t x_27402 = abs(partition_sizze_27390);
    bool empty_slice_27403 = x_27402 == 0;
    int32_t m_27404 = x_27402 - 1;
    bool zzero_leq_i_p_m_t_s_27405 = sle32(0, m_27404);
    bool i_p_m_t_s_leq_w_27406 = slt32(m_27404, partition_sizze_27390);
    bool y_27407 = zzero_leq_i_p_m_t_s_27405 && i_p_m_t_s_leq_w_27406;
    bool ok_or_empty_27408 = empty_slice_27403 || y_27407;
    bool index_certs_27409;
    
    if (!ok_or_empty_27408) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:156:1-164:55 -> tupleTest.fut:162:14-162:19 -> tupleTest.fut:134:13-134:41 -> tupleSparse.fut:187:30-187:69 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", partition_sizze_27390,
                               "] out of bounds for array of shape [",
                               partition_sizze_27390, "].");
        memblock_unref(ctx, &mem_29118, "mem_29118");
        memblock_unref(ctx, &mem_29107, "mem_29107");
        memblock_unref(ctx, &mem_29104, "mem_29104");
        memblock_unref(ctx, &mem_29095, "mem_29095");
        memblock_unref(ctx, &mem_29088, "mem_29088");
        memblock_unref(ctx, &mem_29077, "mem_29077");
        memblock_unref(ctx, &mem_29075, "mem_29075");
        memblock_unref(ctx, &xs_mem_29033, "xs_mem_29033");
        memblock_unref(ctx, &xs_mem_29031, "xs_mem_29031");
        memblock_unref(ctx, &xs_mem_29029, "xs_mem_29029");
        memblock_unref(ctx, &indexed_mem_29072, "indexed_mem_29072");
        memblock_unref(ctx, &indexed_mem_29070, "indexed_mem_29070");
        memblock_unref(ctx, &indexed_mem_29068, "indexed_mem_29068");
        memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
        memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
        memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
        memblock_unref(ctx, &mem_28979, "mem_28979");
        memblock_unref(ctx, &mem_28976, "mem_28976");
        memblock_unref(ctx, &mem_28973, "mem_28973");
        memblock_unref(ctx, &mem_28952, "mem_28952");
        memblock_unref(ctx, &mem_28949, "mem_28949");
        memblock_unref(ctx, &mem_28946, "mem_28946");
        memblock_unref(ctx, &mem_28935, "mem_28935");
        memblock_unref(ctx, &mem_28932, "mem_28932");
        memblock_unref(ctx, &mem_28927, "mem_28927");
        memblock_unref(ctx, &mem_28922, "mem_28922");
        memblock_unref(ctx, &mem_28890, "mem_28890");
        memblock_unref(ctx, &mem_28869, "mem_28869");
        memblock_unref(ctx, &mem_28866, "mem_28866");
        memblock_unref(ctx, &mem_28863, "mem_28863");
        memblock_unref(ctx, &mem_28856, "mem_28856");
        memblock_unref(ctx, &mem_28851, "mem_28851");
        memblock_unref(ctx, &mem_28846, "mem_28846");
        memblock_unref(ctx, &mem_28763, "mem_28763");
        memblock_unref(ctx, &mem_28742, "mem_28742");
        memblock_unref(ctx, &mem_28739, "mem_28739");
        memblock_unref(ctx, &mem_28736, "mem_28736");
        memblock_unref(ctx, &mem_28725, "mem_28725");
        memblock_unref(ctx, &mem_28722, "mem_28722");
        memblock_unref(ctx, &mem_28717, "mem_28717");
        memblock_unref(ctx, &mem_28712, "mem_28712");
        memblock_unref(ctx, &mem_28579, "mem_28579");
        memblock_unref(ctx, &mem_28576, "mem_28576");
        memblock_unref(ctx, &mem_28573, "mem_28573");
        memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
        memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
        memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
        memblock_unref(ctx, &mem_28542, "mem_28542");
        memblock_unref(ctx, &mem_28539, "mem_28539");
        memblock_unref(ctx, &mem_28536, "mem_28536");
        memblock_unref(ctx, &mem_28515, "mem_28515");
        memblock_unref(ctx, &mem_28512, "mem_28512");
        memblock_unref(ctx, &mem_28509, "mem_28509");
        memblock_unref(ctx, &mem_28498, "mem_28498");
        memblock_unref(ctx, &mem_28495, "mem_28495");
        memblock_unref(ctx, &mem_28490, "mem_28490");
        memblock_unref(ctx, &mem_28485, "mem_28485");
        memblock_unref(ctx, &mem_28482, "mem_28482");
        memblock_unref(ctx, &mem_28368, "mem_28368");
        memblock_unref(ctx, &mem_28365, "mem_28365");
        memblock_unref(ctx, &mem_28362, "mem_28362");
        memblock_unref(ctx, &mem_28359, "mem_28359");
        memblock_unref(ctx, &mem_28356, "mem_28356");
        memblock_unref(ctx, &mem_28335, "mem_28335");
        memblock_unref(ctx, &mem_28332, "mem_28332");
        memblock_unref(ctx, &mem_28329, "mem_28329");
        memblock_unref(ctx, &mem_28322, "mem_28322");
        memblock_unref(ctx, &mem_28317, "mem_28317");
        memblock_unref(ctx, &mem_28312, "mem_28312");
        memblock_unref(ctx, &mem_28309, "mem_28309");
        return 1;
    }
    
    bool cond_27411 = x_27402 == 5;
    bool res_27412;
    
    if (cond_27411) {
        bool arrays_equal_27414;
        
        if (cond_27411) {
            bool all_equal_27416;
            bool redout_28074 = 1;
            
            for (int32_t i_28075 = 0; i_28075 < x_27402; i_28075++) {
                int32_t x_27421 = *(int32_t *) &mem_29118.mem[i_28075 * 4];
                int32_t res_27422 = 1 + i_28075;
                bool res_27423 = x_27421 == res_27422;
                bool res_27419 = res_27423 && redout_28074;
                bool redout_tmp_29306 = res_27419;
                
                redout_28074 = redout_tmp_29306;
            }
            all_equal_27416 = redout_28074;
            arrays_equal_27414 = all_equal_27416;
        } else {
            arrays_equal_27414 = 0;
        }
        res_27412 = arrays_equal_27414;
    } else {
        res_27412 = 0;
    }
    memblock_unref(ctx, &mem_29118, "mem_29118");
    
    bool x_27424 = cond_26471 && res_26785;
    bool x_27425 = res_26957 && x_27424;
    bool x_27426 = res_27036 && x_27425;
    bool x_27427 = res_27412 && x_27426;
    
    scalar_out_29125 = x_27427;
    *out_scalar_out_29307 = scalar_out_29125;
    memblock_unref(ctx, &mem_29118, "mem_29118");
    memblock_unref(ctx, &mem_29107, "mem_29107");
    memblock_unref(ctx, &mem_29104, "mem_29104");
    memblock_unref(ctx, &mem_29095, "mem_29095");
    memblock_unref(ctx, &mem_29088, "mem_29088");
    memblock_unref(ctx, &mem_29077, "mem_29077");
    memblock_unref(ctx, &mem_29075, "mem_29075");
    memblock_unref(ctx, &xs_mem_29033, "xs_mem_29033");
    memblock_unref(ctx, &xs_mem_29031, "xs_mem_29031");
    memblock_unref(ctx, &xs_mem_29029, "xs_mem_29029");
    memblock_unref(ctx, &indexed_mem_29072, "indexed_mem_29072");
    memblock_unref(ctx, &indexed_mem_29070, "indexed_mem_29070");
    memblock_unref(ctx, &indexed_mem_29068, "indexed_mem_29068");
    memblock_unref(ctx, &res_mem_29027, "res_mem_29027");
    memblock_unref(ctx, &res_mem_29025, "res_mem_29025");
    memblock_unref(ctx, &res_mem_29023, "res_mem_29023");
    memblock_unref(ctx, &mem_28979, "mem_28979");
    memblock_unref(ctx, &mem_28976, "mem_28976");
    memblock_unref(ctx, &mem_28973, "mem_28973");
    memblock_unref(ctx, &mem_28952, "mem_28952");
    memblock_unref(ctx, &mem_28949, "mem_28949");
    memblock_unref(ctx, &mem_28946, "mem_28946");
    memblock_unref(ctx, &mem_28935, "mem_28935");
    memblock_unref(ctx, &mem_28932, "mem_28932");
    memblock_unref(ctx, &mem_28927, "mem_28927");
    memblock_unref(ctx, &mem_28922, "mem_28922");
    memblock_unref(ctx, &mem_28890, "mem_28890");
    memblock_unref(ctx, &mem_28869, "mem_28869");
    memblock_unref(ctx, &mem_28866, "mem_28866");
    memblock_unref(ctx, &mem_28863, "mem_28863");
    memblock_unref(ctx, &mem_28856, "mem_28856");
    memblock_unref(ctx, &mem_28851, "mem_28851");
    memblock_unref(ctx, &mem_28846, "mem_28846");
    memblock_unref(ctx, &mem_28763, "mem_28763");
    memblock_unref(ctx, &mem_28742, "mem_28742");
    memblock_unref(ctx, &mem_28739, "mem_28739");
    memblock_unref(ctx, &mem_28736, "mem_28736");
    memblock_unref(ctx, &mem_28725, "mem_28725");
    memblock_unref(ctx, &mem_28722, "mem_28722");
    memblock_unref(ctx, &mem_28717, "mem_28717");
    memblock_unref(ctx, &mem_28712, "mem_28712");
    memblock_unref(ctx, &mem_28579, "mem_28579");
    memblock_unref(ctx, &mem_28576, "mem_28576");
    memblock_unref(ctx, &mem_28573, "mem_28573");
    memblock_unref(ctx, &res_mem_28570, "res_mem_28570");
    memblock_unref(ctx, &res_mem_28568, "res_mem_28568");
    memblock_unref(ctx, &res_mem_28566, "res_mem_28566");
    memblock_unref(ctx, &mem_28542, "mem_28542");
    memblock_unref(ctx, &mem_28539, "mem_28539");
    memblock_unref(ctx, &mem_28536, "mem_28536");
    memblock_unref(ctx, &mem_28515, "mem_28515");
    memblock_unref(ctx, &mem_28512, "mem_28512");
    memblock_unref(ctx, &mem_28509, "mem_28509");
    memblock_unref(ctx, &mem_28498, "mem_28498");
    memblock_unref(ctx, &mem_28495, "mem_28495");
    memblock_unref(ctx, &mem_28490, "mem_28490");
    memblock_unref(ctx, &mem_28485, "mem_28485");
    memblock_unref(ctx, &mem_28482, "mem_28482");
    memblock_unref(ctx, &mem_28368, "mem_28368");
    memblock_unref(ctx, &mem_28365, "mem_28365");
    memblock_unref(ctx, &mem_28362, "mem_28362");
    memblock_unref(ctx, &mem_28359, "mem_28359");
    memblock_unref(ctx, &mem_28356, "mem_28356");
    memblock_unref(ctx, &mem_28335, "mem_28335");
    memblock_unref(ctx, &mem_28332, "mem_28332");
    memblock_unref(ctx, &mem_28329, "mem_28329");
    memblock_unref(ctx, &mem_28322, "mem_28322");
    memblock_unref(ctx, &mem_28317, "mem_28317");
    memblock_unref(ctx, &mem_28312, "mem_28312");
    memblock_unref(ctx, &mem_28309, "mem_28309");
    return 0;
}
int futhark_entry_main(struct futhark_context *ctx, bool *out0)
{
    bool scalar_out_29125;
    
    lock_lock(&ctx->lock);
    
    int ret = futrts_main(ctx, &scalar_out_29125);
    
    if (ret == 0) {
        *out0 = scalar_out_29125;
    }
    lock_unlock(&ctx->lock);
    return ret;
}
