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
    bool result_48511;
    
    if (perform_warmup) {
        time_runs = 0;
        
        int r;
        
        assert(futhark_context_sync(ctx) == 0);
        t_start = get_wall_time();
        r = futhark_entry_main(ctx, &result_48511);
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
        r = futhark_entry_main(ctx, &result_48511);
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
    write_scalar(stdout, binary_output, &bool_info, &result_48511);
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

static int32_t static_array_realtype_48502[4] = {1, 2, 3, 4};
static int32_t static_array_realtype_48503[4] = {0, 0, 1, 1};
static int32_t static_array_realtype_48504[4] = {0, 1, 0, 1};
static int32_t static_array_realtype_48505[3] = {0, 1, 1};
static int32_t static_array_realtype_48506[3] = {1, 0, 1};
static int32_t static_array_realtype_48507[3] = {1, 2, 3};
static int32_t static_array_realtype_48508[3] = {1, 4, 3};
static int32_t static_array_realtype_48509[4] = {0, 1, 1, 0};
static int32_t static_array_realtype_48510[4] = {1, 0, 1, 0};
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
    struct memblock static_array_48091;
    struct memblock static_array_48102;
    struct memblock static_array_48103;
    struct memblock static_array_48107;
    struct memblock static_array_48108;
    struct memblock static_array_48109;
    struct memblock static_array_48159;
    struct memblock static_array_48163;
    struct memblock static_array_48164;
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
    ctx->static_array_48091 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48502,
                                                 0};
    ctx->static_array_48102 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48503,
                                                 0};
    ctx->static_array_48103 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48504,
                                                 0};
    ctx->static_array_48107 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48505,
                                                 0};
    ctx->static_array_48108 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48506,
                                                 0};
    ctx->static_array_48109 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48507,
                                                 0};
    ctx->static_array_48159 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48508,
                                                 0};
    ctx->static_array_48163 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48509,
                                                 0};
    ctx->static_array_48164 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_48510,
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
static int memblock_unref(struct futhark_context *ctx, struct memblock *block,
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
    return 0;
}
static int memblock_alloc(struct futhark_context *ctx, struct memblock *block,
                          int64_t size, const char *desc)
{
    if (size < 0)
        panic(1, "Negative allocation of %lld bytes attempted for %s in %s.\n",
              (long long) size, desc, "default space",
              ctx->cur_mem_usage_default);
    
    int ret = memblock_unref(ctx, block, desc);
    
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
    return ret;
}
static int memblock_set(struct futhark_context *ctx, struct memblock *lhs,
                        struct memblock *rhs, const char *lhs_desc)
{
    int ret = memblock_unref(ctx, lhs, lhs_desc);
    
    (*rhs->references)++;
    *lhs = *rhs;
    return ret;
}
void futhark_debugging_report(struct futhark_context *ctx)
{
    if (ctx->detail_memory) {
        fprintf(stderr, "Peak memory usage for default space: %lld bytes.\n",
                (long long) ctx->peak_mem_usage_default);
    }
    if (ctx->debugging) { }
}
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_48501);
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
static inline bool itob_i8_bool(int8_t x)
{
    return x;
}
static inline bool itob_i16_bool(int16_t x)
{
    return x;
}
static inline bool itob_i32_bool(int32_t x)
{
    return x;
}
static inline bool itob_i64_bool(int64_t x)
{
    return x;
}
static inline int8_t btoi_bool_i8(bool x)
{
    return x;
}
static inline int16_t btoi_bool_i16(bool x)
{
    return x;
}
static inline int32_t btoi_bool_i32(bool x)
{
    return x;
}
static inline int64_t btoi_bool_i64(bool x)
{
    return x;
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
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_48501)
{
    bool scalar_out_48090;
    struct memblock mem_46642;
    
    mem_46642.references = NULL;
    if (memblock_alloc(ctx, &mem_46642, 16, "mem_46642"))
        return 1;
    
    struct memblock static_array_48091 = ctx->static_array_48091;
    
    memmove(mem_46642.mem + 0, static_array_48091.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_46645;
    
    mem_46645.references = NULL;
    if (memblock_alloc(ctx, &mem_46645, 16, "mem_46645"))
        return 1;
    
    struct memblock mem_46650;
    
    mem_46650.references = NULL;
    if (memblock_alloc(ctx, &mem_46650, 8, "mem_46650"))
        return 1;
    for (int32_t i_45065 = 0; i_45065 < 2; i_45065++) {
        for (int32_t i_48093 = 0; i_48093 < 2; i_48093++) {
            *(int32_t *) &mem_46650.mem[i_48093 * 4] = i_45065;
        }
        memmove(mem_46645.mem + 2 * i_45065 * 4, mem_46650.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
        return 1;
    
    struct memblock mem_46655;
    
    mem_46655.references = NULL;
    if (memblock_alloc(ctx, &mem_46655, 16, "mem_46655"))
        return 1;
    
    struct memblock mem_46658;
    
    mem_46658.references = NULL;
    if (memblock_alloc(ctx, &mem_46658, 16, "mem_46658"))
        return 1;
    
    int32_t discard_45075;
    int32_t scanacc_45069 = 0;
    
    for (int32_t i_45072 = 0; i_45072 < 4; i_45072++) {
        int32_t zz_42949 = 1 + scanacc_45069;
        
        *(int32_t *) &mem_46655.mem[i_45072 * 4] = zz_42949;
        *(int32_t *) &mem_46658.mem[i_45072 * 4] = 0;
        
        int32_t scanacc_tmp_48094 = zz_42949;
        
        scanacc_45069 = scanacc_tmp_48094;
    }
    discard_45075 = scanacc_45069;
    
    int32_t last_offset_42955 = *(int32_t *) &mem_46655.mem[12];
    int64_t binop_x_46668 = sext_i32_i64(last_offset_42955);
    int64_t bytes_46667 = 4 * binop_x_46668;
    struct memblock mem_46669;
    
    mem_46669.references = NULL;
    if (memblock_alloc(ctx, &mem_46669, bytes_46667, "mem_46669"))
        return 1;
    
    struct memblock mem_46672;
    
    mem_46672.references = NULL;
    if (memblock_alloc(ctx, &mem_46672, bytes_46667, "mem_46672"))
        return 1;
    
    struct memblock mem_46675;
    
    mem_46675.references = NULL;
    if (memblock_alloc(ctx, &mem_46675, bytes_46667, "mem_46675"))
        return 1;
    for (int32_t write_iter_45076 = 0; write_iter_45076 < 4;
         write_iter_45076++) {
        int32_t write_iv_45080 = *(int32_t *) &mem_46658.mem[write_iter_45076 *
                                                             4];
        int32_t write_iv_45081 = *(int32_t *) &mem_46655.mem[write_iter_45076 *
                                                             4];
        int32_t new_index_46167 = squot32(write_iter_45076, 2);
        int32_t binop_y_46169 = 2 * new_index_46167;
        int32_t new_index_46170 = write_iter_45076 - binop_y_46169;
        bool is_this_one_42967 = write_iv_45080 == 0;
        int32_t this_offset_42968 = -1 + write_iv_45081;
        int32_t total_res_42969;
        
        if (is_this_one_42967) {
            total_res_42969 = this_offset_42968;
        } else {
            total_res_42969 = -1;
        }
        
        bool less_than_zzero_45085 = slt32(total_res_42969, 0);
        bool greater_than_sizze_45086 = sle32(last_offset_42955,
                                              total_res_42969);
        bool outside_bounds_dim_45087 = less_than_zzero_45085 ||
             greater_than_sizze_45086;
        
        if (!outside_bounds_dim_45087) {
            memmove(mem_46669.mem + total_res_42969 * 4, mem_46645.mem + (2 *
                                                                          new_index_46167 +
                                                                          new_index_46170) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_45087) {
            struct memblock mem_46684;
            
            mem_46684.references = NULL;
            if (memblock_alloc(ctx, &mem_46684, 4, "mem_46684"))
                return 1;
            
            int32_t x_48101;
            
            for (int32_t i_48100 = 0; i_48100 < 1; i_48100++) {
                x_48101 = new_index_46170 + sext_i32_i32(i_48100);
                *(int32_t *) &mem_46684.mem[i_48100 * 4] = x_48101;
            }
            memmove(mem_46672.mem + total_res_42969 * 4, mem_46684.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_46684, "mem_46684") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46684, "mem_46684") != 0)
                return 1;
        }
        if (!outside_bounds_dim_45087) {
            memmove(mem_46675.mem + total_res_42969 * 4, mem_46642.mem +
                    write_iter_45076 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
        return 1;
    
    struct memblock mem_46693;
    
    mem_46693.references = NULL;
    if (memblock_alloc(ctx, &mem_46693, 16, "mem_46693"))
        return 1;
    
    struct memblock static_array_48102 = ctx->static_array_48102;
    
    memmove(mem_46693.mem + 0, static_array_48102.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_46696;
    
    mem_46696.references = NULL;
    if (memblock_alloc(ctx, &mem_46696, 16, "mem_46696"))
        return 1;
    
    struct memblock static_array_48103 = ctx->static_array_48103;
    
    memmove(mem_46696.mem + 0, static_array_48103.mem + 0, 4 * sizeof(int32_t));
    
    bool dim_eq_42972 = last_offset_42955 == 4;
    bool arrays_equal_42973;
    
    if (dim_eq_42972) {
        bool all_equal_42975;
        bool redout_45103 = 1;
        
        for (int32_t i_45104 = 0; i_45104 < last_offset_42955; i_45104++) {
            int32_t x_42979 = *(int32_t *) &mem_46669.mem[i_45104 * 4];
            int32_t y_42980 = *(int32_t *) &mem_46693.mem[i_45104 * 4];
            bool res_42981 = x_42979 == y_42980;
            bool res_42978 = res_42981 && redout_45103;
            bool redout_tmp_48104 = res_42978;
            
            redout_45103 = redout_tmp_48104;
        }
        all_equal_42975 = redout_45103;
        arrays_equal_42973 = all_equal_42975;
    } else {
        arrays_equal_42973 = 0;
    }
    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
        return 1;
    
    bool arrays_equal_42982;
    
    if (dim_eq_42972) {
        bool all_equal_42984;
        bool redout_45105 = 1;
        
        for (int32_t i_45106 = 0; i_45106 < last_offset_42955; i_45106++) {
            int32_t x_42988 = *(int32_t *) &mem_46672.mem[i_45106 * 4];
            int32_t y_42989 = *(int32_t *) &mem_46696.mem[i_45106 * 4];
            bool res_42990 = x_42988 == y_42989;
            bool res_42987 = res_42990 && redout_45105;
            bool redout_tmp_48105 = res_42987;
            
            redout_45105 = redout_tmp_48105;
        }
        all_equal_42984 = redout_45105;
        arrays_equal_42982 = all_equal_42984;
    } else {
        arrays_equal_42982 = 0;
    }
    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
        return 1;
    
    bool eq_42991 = arrays_equal_42973 && arrays_equal_42982;
    bool res_42992;
    
    if (eq_42991) {
        bool arrays_equal_42993;
        
        if (dim_eq_42972) {
            bool all_equal_42995;
            bool redout_45107 = 1;
            
            for (int32_t i_45108 = 0; i_45108 < last_offset_42955; i_45108++) {
                int32_t x_42999 = *(int32_t *) &mem_46675.mem[i_45108 * 4];
                int32_t y_43000 = *(int32_t *) &mem_46642.mem[i_45108 * 4];
                bool res_43001 = x_42999 == y_43000;
                bool res_42998 = res_43001 && redout_45107;
                bool redout_tmp_48106 = res_42998;
                
                redout_45107 = redout_tmp_48106;
            }
            all_equal_42995 = redout_45107;
            arrays_equal_42993 = all_equal_42995;
        } else {
            arrays_equal_42993 = 0;
        }
        res_42992 = arrays_equal_42993;
    } else {
        res_42992 = 0;
    }
    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
        return 1;
    
    struct memblock mem_46699;
    
    mem_46699.references = NULL;
    if (memblock_alloc(ctx, &mem_46699, 12, "mem_46699"))
        return 1;
    
    struct memblock static_array_48107 = ctx->static_array_48107;
    
    memmove(mem_46699.mem + 0, static_array_48107.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_46702;
    
    mem_46702.references = NULL;
    if (memblock_alloc(ctx, &mem_46702, 12, "mem_46702"))
        return 1;
    
    struct memblock static_array_48108 = ctx->static_array_48108;
    
    memmove(mem_46702.mem + 0, static_array_48108.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_46705;
    
    mem_46705.references = NULL;
    if (memblock_alloc(ctx, &mem_46705, 12, "mem_46705"))
        return 1;
    
    struct memblock static_array_48109 = ctx->static_array_48109;
    
    memmove(mem_46705.mem + 0, static_array_48109.mem + 0, 3 * sizeof(int32_t));
    
    bool cond_43006;
    
    if (res_42992) {
        struct memblock mem_46708;
        
        mem_46708.references = NULL;
        if (memblock_alloc(ctx, &mem_46708, 16, "mem_46708"))
            return 1;
        
        struct memblock mem_46713;
        
        mem_46713.references = NULL;
        if (memblock_alloc(ctx, &mem_46713, 8, "mem_46713"))
            return 1;
        for (int32_t i_45111 = 0; i_45111 < 2; i_45111++) {
            for (int32_t i_48111 = 0; i_48111 < 2; i_48111++) {
                *(int32_t *) &mem_46713.mem[i_48111 * 4] = i_45111;
            }
            memmove(mem_46708.mem + 2 * i_45111 * 4, mem_46713.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_46713, "mem_46713") != 0)
            return 1;
        
        struct memblock mem_46718;
        
        mem_46718.references = NULL;
        if (memblock_alloc(ctx, &mem_46718, 16, "mem_46718"))
            return 1;
        
        struct memblock mem_46721;
        
        mem_46721.references = NULL;
        if (memblock_alloc(ctx, &mem_46721, 16, "mem_46721"))
            return 1;
        
        int32_t discard_45121;
        int32_t scanacc_45115 = 0;
        
        for (int32_t i_45118 = 0; i_45118 < 4; i_45118++) {
            bool not_arg_43017 = i_45118 == 0;
            bool res_43018 = !not_arg_43017;
            int32_t part_res_43019;
            
            if (res_43018) {
                part_res_43019 = 0;
            } else {
                part_res_43019 = 1;
            }
            
            int32_t part_res_43020;
            
            if (res_43018) {
                part_res_43020 = 1;
            } else {
                part_res_43020 = 0;
            }
            
            int32_t zz_43015 = part_res_43020 + scanacc_45115;
            
            *(int32_t *) &mem_46718.mem[i_45118 * 4] = zz_43015;
            *(int32_t *) &mem_46721.mem[i_45118 * 4] = part_res_43019;
            
            int32_t scanacc_tmp_48112 = zz_43015;
            
            scanacc_45115 = scanacc_tmp_48112;
        }
        discard_45121 = scanacc_45115;
        
        int32_t last_offset_43021 = *(int32_t *) &mem_46718.mem[12];
        int64_t binop_x_46731 = sext_i32_i64(last_offset_43021);
        int64_t bytes_46730 = 4 * binop_x_46731;
        struct memblock mem_46732;
        
        mem_46732.references = NULL;
        if (memblock_alloc(ctx, &mem_46732, bytes_46730, "mem_46732"))
            return 1;
        
        struct memblock mem_46735;
        
        mem_46735.references = NULL;
        if (memblock_alloc(ctx, &mem_46735, bytes_46730, "mem_46735"))
            return 1;
        
        struct memblock mem_46738;
        
        mem_46738.references = NULL;
        if (memblock_alloc(ctx, &mem_46738, bytes_46730, "mem_46738"))
            return 1;
        for (int32_t write_iter_45122 = 0; write_iter_45122 < 4;
             write_iter_45122++) {
            int32_t write_iv_45126 =
                    *(int32_t *) &mem_46721.mem[write_iter_45122 * 4];
            int32_t write_iv_45127 =
                    *(int32_t *) &mem_46718.mem[write_iter_45122 * 4];
            int32_t new_index_46183 = squot32(write_iter_45122, 2);
            int32_t binop_y_46185 = 2 * new_index_46183;
            int32_t new_index_46186 = write_iter_45122 - binop_y_46185;
            bool is_this_one_43033 = write_iv_45126 == 0;
            int32_t this_offset_43034 = -1 + write_iv_45127;
            int32_t total_res_43035;
            
            if (is_this_one_43033) {
                total_res_43035 = this_offset_43034;
            } else {
                total_res_43035 = -1;
            }
            
            bool less_than_zzero_45131 = slt32(total_res_43035, 0);
            bool greater_than_sizze_45132 = sle32(last_offset_43021,
                                                  total_res_43035);
            bool outside_bounds_dim_45133 = less_than_zzero_45131 ||
                 greater_than_sizze_45132;
            
            if (!outside_bounds_dim_45133) {
                memmove(mem_46732.mem + total_res_43035 * 4, mem_46708.mem +
                        (2 * new_index_46183 + new_index_46186) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45133) {
                struct memblock mem_46747;
                
                mem_46747.references = NULL;
                if (memblock_alloc(ctx, &mem_46747, 4, "mem_46747"))
                    return 1;
                
                int32_t x_48119;
                
                for (int32_t i_48118 = 0; i_48118 < 1; i_48118++) {
                    x_48119 = new_index_46186 + sext_i32_i32(i_48118);
                    *(int32_t *) &mem_46747.mem[i_48118 * 4] = x_48119;
                }
                memmove(mem_46735.mem + total_res_43035 * 4, mem_46747.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_46747, "mem_46747") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46747, "mem_46747") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45133) {
                struct memblock mem_46750;
                
                mem_46750.references = NULL;
                if (memblock_alloc(ctx, &mem_46750, 4, "mem_46750"))
                    return 1;
                
                int32_t x_48121;
                
                for (int32_t i_48120 = 0; i_48120 < 1; i_48120++) {
                    x_48121 = write_iter_45122 + sext_i32_i32(i_48120);
                    *(int32_t *) &mem_46750.mem[i_48120 * 4] = x_48121;
                }
                memmove(mem_46738.mem + total_res_43035 * 4, mem_46750.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_46750, "mem_46750") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46750, "mem_46750") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_46708, "mem_46708") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46718, "mem_46718") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46721, "mem_46721") != 0)
            return 1;
        
        bool dim_eq_43036 = last_offset_43021 == 3;
        bool arrays_equal_43037;
        
        if (dim_eq_43036) {
            bool all_equal_43039;
            bool redout_45149 = 1;
            
            for (int32_t i_45150 = 0; i_45150 < last_offset_43021; i_45150++) {
                int32_t x_43043 = *(int32_t *) &mem_46732.mem[i_45150 * 4];
                int32_t y_43044 = *(int32_t *) &mem_46699.mem[i_45150 * 4];
                bool res_43045 = x_43043 == y_43044;
                bool res_43042 = res_43045 && redout_45149;
                bool redout_tmp_48122 = res_43042;
                
                redout_45149 = redout_tmp_48122;
            }
            all_equal_43039 = redout_45149;
            arrays_equal_43037 = all_equal_43039;
        } else {
            arrays_equal_43037 = 0;
        }
        if (memblock_unref(ctx, &mem_46732, "mem_46732") != 0)
            return 1;
        
        bool arrays_equal_43046;
        
        if (dim_eq_43036) {
            bool all_equal_43048;
            bool redout_45151 = 1;
            
            for (int32_t i_45152 = 0; i_45152 < last_offset_43021; i_45152++) {
                int32_t x_43052 = *(int32_t *) &mem_46735.mem[i_45152 * 4];
                int32_t y_43053 = *(int32_t *) &mem_46702.mem[i_45152 * 4];
                bool res_43054 = x_43052 == y_43053;
                bool res_43051 = res_43054 && redout_45151;
                bool redout_tmp_48123 = res_43051;
                
                redout_45151 = redout_tmp_48123;
            }
            all_equal_43048 = redout_45151;
            arrays_equal_43046 = all_equal_43048;
        } else {
            arrays_equal_43046 = 0;
        }
        if (memblock_unref(ctx, &mem_46735, "mem_46735") != 0)
            return 1;
        
        bool eq_43055 = arrays_equal_43037 && arrays_equal_43046;
        bool res_43056;
        
        if (eq_43055) {
            bool arrays_equal_43057;
            
            if (dim_eq_43036) {
                bool all_equal_43059;
                bool redout_45153 = 1;
                
                for (int32_t i_45154 = 0; i_45154 < last_offset_43021;
                     i_45154++) {
                    int32_t x_43063 = *(int32_t *) &mem_46738.mem[i_45154 * 4];
                    int32_t y_43064 = *(int32_t *) &mem_46705.mem[i_45154 * 4];
                    bool res_43065 = x_43063 == y_43064;
                    bool res_43062 = res_43065 && redout_45153;
                    bool redout_tmp_48124 = res_43062;
                    
                    redout_45153 = redout_tmp_48124;
                }
                all_equal_43059 = redout_45153;
                arrays_equal_43057 = all_equal_43059;
            } else {
                arrays_equal_43057 = 0;
            }
            res_43056 = arrays_equal_43057;
        } else {
            res_43056 = 0;
        }
        if (memblock_unref(ctx, &mem_46738, "mem_46738") != 0)
            return 1;
        cond_43006 = res_43056;
        if (memblock_unref(ctx, &mem_46738, "mem_46738") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46735, "mem_46735") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46732, "mem_46732") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46721, "mem_46721") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46718, "mem_46718") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46713, "mem_46713") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46708, "mem_46708") != 0)
            return 1;
    } else {
        cond_43006 = 0;
    }
    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
        return 1;
    
    bool cond_43066;
    
    if (cond_43006) {
        struct memblock mem_46759;
        
        mem_46759.references = NULL;
        if (memblock_alloc(ctx, &mem_46759, 16, "mem_46759"))
            return 1;
        
        struct memblock mem_46764;
        
        mem_46764.references = NULL;
        if (memblock_alloc(ctx, &mem_46764, 8, "mem_46764"))
            return 1;
        for (int32_t i_45157 = 0; i_45157 < 2; i_45157++) {
            for (int32_t i_48126 = 0; i_48126 < 2; i_48126++) {
                *(int32_t *) &mem_46764.mem[i_48126 * 4] = i_45157;
            }
            memmove(mem_46759.mem + 2 * i_45157 * 4, mem_46764.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_46764, "mem_46764") != 0)
            return 1;
        
        struct memblock mem_46769;
        
        mem_46769.references = NULL;
        if (memblock_alloc(ctx, &mem_46769, 16, "mem_46769"))
            return 1;
        
        struct memblock mem_46772;
        
        mem_46772.references = NULL;
        if (memblock_alloc(ctx, &mem_46772, 16, "mem_46772"))
            return 1;
        
        int32_t discard_45167;
        int32_t scanacc_45161 = 0;
        
        for (int32_t i_45164 = 0; i_45164 < 4; i_45164++) {
            bool not_arg_43077 = i_45164 == 0;
            bool res_43078 = !not_arg_43077;
            int32_t part_res_43079;
            
            if (res_43078) {
                part_res_43079 = 0;
            } else {
                part_res_43079 = 1;
            }
            
            int32_t part_res_43080;
            
            if (res_43078) {
                part_res_43080 = 1;
            } else {
                part_res_43080 = 0;
            }
            
            int32_t zz_43075 = part_res_43080 + scanacc_45161;
            
            *(int32_t *) &mem_46769.mem[i_45164 * 4] = zz_43075;
            *(int32_t *) &mem_46772.mem[i_45164 * 4] = part_res_43079;
            
            int32_t scanacc_tmp_48127 = zz_43075;
            
            scanacc_45161 = scanacc_tmp_48127;
        }
        discard_45167 = scanacc_45161;
        
        int32_t last_offset_43081 = *(int32_t *) &mem_46769.mem[12];
        int64_t binop_x_46782 = sext_i32_i64(last_offset_43081);
        int64_t bytes_46781 = 4 * binop_x_46782;
        struct memblock mem_46783;
        
        mem_46783.references = NULL;
        if (memblock_alloc(ctx, &mem_46783, bytes_46781, "mem_46783"))
            return 1;
        
        struct memblock mem_46786;
        
        mem_46786.references = NULL;
        if (memblock_alloc(ctx, &mem_46786, bytes_46781, "mem_46786"))
            return 1;
        
        struct memblock mem_46789;
        
        mem_46789.references = NULL;
        if (memblock_alloc(ctx, &mem_46789, bytes_46781, "mem_46789"))
            return 1;
        for (int32_t write_iter_45168 = 0; write_iter_45168 < 4;
             write_iter_45168++) {
            int32_t write_iv_45172 =
                    *(int32_t *) &mem_46772.mem[write_iter_45168 * 4];
            int32_t write_iv_45173 =
                    *(int32_t *) &mem_46769.mem[write_iter_45168 * 4];
            int32_t new_index_46202 = squot32(write_iter_45168, 2);
            int32_t binop_y_46204 = 2 * new_index_46202;
            int32_t new_index_46205 = write_iter_45168 - binop_y_46204;
            bool is_this_one_43093 = write_iv_45172 == 0;
            int32_t this_offset_43094 = -1 + write_iv_45173;
            int32_t total_res_43095;
            
            if (is_this_one_43093) {
                total_res_43095 = this_offset_43094;
            } else {
                total_res_43095 = -1;
            }
            
            bool less_than_zzero_45177 = slt32(total_res_43095, 0);
            bool greater_than_sizze_45178 = sle32(last_offset_43081,
                                                  total_res_43095);
            bool outside_bounds_dim_45179 = less_than_zzero_45177 ||
                 greater_than_sizze_45178;
            
            if (!outside_bounds_dim_45179) {
                memmove(mem_46783.mem + total_res_43095 * 4, mem_46759.mem +
                        (2 * new_index_46202 + new_index_46205) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45179) {
                struct memblock mem_46798;
                
                mem_46798.references = NULL;
                if (memblock_alloc(ctx, &mem_46798, 4, "mem_46798"))
                    return 1;
                
                int32_t x_48134;
                
                for (int32_t i_48133 = 0; i_48133 < 1; i_48133++) {
                    x_48134 = new_index_46205 + sext_i32_i32(i_48133);
                    *(int32_t *) &mem_46798.mem[i_48133 * 4] = x_48134;
                }
                memmove(mem_46786.mem + total_res_43095 * 4, mem_46798.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_46798, "mem_46798") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46798, "mem_46798") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45179) {
                struct memblock mem_46801;
                
                mem_46801.references = NULL;
                if (memblock_alloc(ctx, &mem_46801, 4, "mem_46801"))
                    return 1;
                
                int32_t x_48136;
                
                for (int32_t i_48135 = 0; i_48135 < 1; i_48135++) {
                    x_48136 = write_iter_45168 + sext_i32_i32(i_48135);
                    *(int32_t *) &mem_46801.mem[i_48135 * 4] = x_48136;
                }
                memmove(mem_46789.mem + total_res_43095 * 4, mem_46801.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_46801, "mem_46801") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46801, "mem_46801") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_46759, "mem_46759") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46769, "mem_46769") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46772, "mem_46772") != 0)
            return 1;
        
        struct memblock mem_46810;
        
        mem_46810.references = NULL;
        if (memblock_alloc(ctx, &mem_46810, 16, "mem_46810"))
            return 1;
        for (int32_t i_48137 = 0; i_48137 < 4; i_48137++) {
            *(int32_t *) &mem_46810.mem[i_48137 * 4] = 0;
        }
        for (int32_t write_iter_45195 = 0; write_iter_45195 < last_offset_43081;
             write_iter_45195++) {
            int32_t write_iv_45197 =
                    *(int32_t *) &mem_46783.mem[write_iter_45195 * 4];
            int32_t write_iv_45198 =
                    *(int32_t *) &mem_46786.mem[write_iter_45195 * 4];
            int32_t x_43101 = 2 * write_iv_45197;
            int32_t res_43102 = x_43101 + write_iv_45198;
            bool less_than_zzero_45200 = slt32(res_43102, 0);
            bool greater_than_sizze_45201 = sle32(4, res_43102);
            bool outside_bounds_dim_45202 = less_than_zzero_45200 ||
                 greater_than_sizze_45201;
            
            if (!outside_bounds_dim_45202) {
                memmove(mem_46810.mem + res_43102 * 4, mem_46789.mem +
                        write_iter_45195 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_46783, "mem_46783") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46786, "mem_46786") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46789, "mem_46789") != 0)
            return 1;
        
        bool all_equal_43103;
        bool redout_45206 = 1;
        
        for (int32_t i_45207 = 0; i_45207 < 4; i_45207++) {
            int32_t y_43108 = *(int32_t *) &mem_46810.mem[i_45207 * 4];
            bool res_43109 = i_45207 == y_43108;
            bool res_43106 = res_43109 && redout_45206;
            bool redout_tmp_48139 = res_43106;
            
            redout_45206 = redout_tmp_48139;
        }
        all_equal_43103 = redout_45206;
        if (memblock_unref(ctx, &mem_46810, "mem_46810") != 0)
            return 1;
        cond_43066 = all_equal_43103;
        if (memblock_unref(ctx, &mem_46810, "mem_46810") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46789, "mem_46789") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46786, "mem_46786") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46783, "mem_46783") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46772, "mem_46772") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46769, "mem_46769") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46764, "mem_46764") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46759, "mem_46759") != 0)
            return 1;
    } else {
        cond_43066 = 0;
    }
    
    struct memblock mem_46817;
    
    mem_46817.references = NULL;
    if (memblock_alloc(ctx, &mem_46817, 0, "mem_46817"))
        return 1;
    
    struct memblock mem_46820;
    
    mem_46820.references = NULL;
    if (memblock_alloc(ctx, &mem_46820, 16, "mem_46820"))
        return 1;
    
    struct memblock mem_46825;
    
    mem_46825.references = NULL;
    if (memblock_alloc(ctx, &mem_46825, 8, "mem_46825"))
        return 1;
    for (int32_t i_45223 = 0; i_45223 < 2; i_45223++) {
        for (int32_t i_48141 = 0; i_48141 < 2; i_48141++) {
            *(int32_t *) &mem_46825.mem[i_48141 * 4] = i_45223;
        }
        memmove(mem_46820.mem + 2 * i_45223 * 4, mem_46825.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
        return 1;
    
    struct memblock mem_46830;
    
    mem_46830.references = NULL;
    if (memblock_alloc(ctx, &mem_46830, 16, "mem_46830"))
        return 1;
    
    struct memblock mem_46833;
    
    mem_46833.references = NULL;
    if (memblock_alloc(ctx, &mem_46833, 16, "mem_46833"))
        return 1;
    
    int32_t discard_45233;
    int32_t scanacc_45227 = 0;
    
    for (int32_t i_45230 = 0; i_45230 < 4; i_45230++) {
        bool not_arg_43138 = i_45230 == 0;
        bool res_43139 = !not_arg_43138;
        int32_t part_res_43140;
        
        if (res_43139) {
            part_res_43140 = 0;
        } else {
            part_res_43140 = 1;
        }
        
        int32_t part_res_43141;
        
        if (res_43139) {
            part_res_43141 = 1;
        } else {
            part_res_43141 = 0;
        }
        
        int32_t zz_43136 = part_res_43141 + scanacc_45227;
        
        *(int32_t *) &mem_46830.mem[i_45230 * 4] = zz_43136;
        *(int32_t *) &mem_46833.mem[i_45230 * 4] = part_res_43140;
        
        int32_t scanacc_tmp_48142 = zz_43136;
        
        scanacc_45227 = scanacc_tmp_48142;
    }
    discard_45233 = scanacc_45227;
    
    int32_t last_offset_43142 = *(int32_t *) &mem_46830.mem[12];
    int64_t binop_x_46843 = sext_i32_i64(last_offset_43142);
    int64_t bytes_46842 = 4 * binop_x_46843;
    struct memblock mem_46844;
    
    mem_46844.references = NULL;
    if (memblock_alloc(ctx, &mem_46844, bytes_46842, "mem_46844"))
        return 1;
    
    struct memblock mem_46847;
    
    mem_46847.references = NULL;
    if (memblock_alloc(ctx, &mem_46847, bytes_46842, "mem_46847"))
        return 1;
    
    struct memblock mem_46850;
    
    mem_46850.references = NULL;
    if (memblock_alloc(ctx, &mem_46850, bytes_46842, "mem_46850"))
        return 1;
    for (int32_t write_iter_45234 = 0; write_iter_45234 < 4;
         write_iter_45234++) {
        int32_t write_iv_45238 = *(int32_t *) &mem_46833.mem[write_iter_45234 *
                                                             4];
        int32_t write_iv_45239 = *(int32_t *) &mem_46830.mem[write_iter_45234 *
                                                             4];
        int32_t new_index_46232 = squot32(write_iter_45234, 2);
        int32_t binop_y_46234 = 2 * new_index_46232;
        int32_t new_index_46235 = write_iter_45234 - binop_y_46234;
        bool is_this_one_43154 = write_iv_45238 == 0;
        int32_t this_offset_43155 = -1 + write_iv_45239;
        int32_t total_res_43156;
        
        if (is_this_one_43154) {
            total_res_43156 = this_offset_43155;
        } else {
            total_res_43156 = -1;
        }
        
        bool less_than_zzero_45243 = slt32(total_res_43156, 0);
        bool greater_than_sizze_45244 = sle32(last_offset_43142,
                                              total_res_43156);
        bool outside_bounds_dim_45245 = less_than_zzero_45243 ||
             greater_than_sizze_45244;
        
        if (!outside_bounds_dim_45245) {
            memmove(mem_46844.mem + total_res_43156 * 4, mem_46820.mem + (2 *
                                                                          new_index_46232 +
                                                                          new_index_46235) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_45245) {
            struct memblock mem_46859;
            
            mem_46859.references = NULL;
            if (memblock_alloc(ctx, &mem_46859, 4, "mem_46859"))
                return 1;
            
            int32_t x_48149;
            
            for (int32_t i_48148 = 0; i_48148 < 1; i_48148++) {
                x_48149 = new_index_46235 + sext_i32_i32(i_48148);
                *(int32_t *) &mem_46859.mem[i_48148 * 4] = x_48149;
            }
            memmove(mem_46847.mem + total_res_43156 * 4, mem_46859.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_46859, "mem_46859") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46859, "mem_46859") != 0)
                return 1;
        }
        if (!outside_bounds_dim_45245) {
            struct memblock mem_46862;
            
            mem_46862.references = NULL;
            if (memblock_alloc(ctx, &mem_46862, 4, "mem_46862"))
                return 1;
            
            int32_t x_48151;
            
            for (int32_t i_48150 = 0; i_48150 < 1; i_48150++) {
                x_48151 = write_iter_45234 + sext_i32_i32(i_48150);
                *(int32_t *) &mem_46862.mem[i_48150 * 4] = x_48151;
            }
            memmove(mem_46850.mem + total_res_43156 * 4, mem_46862.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_46862, "mem_46862") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46862, "mem_46862") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
        return 1;
    
    struct memblock mem_46871;
    
    mem_46871.references = NULL;
    if (memblock_alloc(ctx, &mem_46871, 4, "mem_46871"))
        return 1;
    for (int32_t i_48152 = 0; i_48152 < 1; i_48152++) {
        *(int32_t *) &mem_46871.mem[i_48152 * 4] = 1;
    }
    
    struct memblock mem_46874;
    
    mem_46874.references = NULL;
    if (memblock_alloc(ctx, &mem_46874, 4, "mem_46874"))
        return 1;
    for (int32_t i_48153 = 0; i_48153 < 1; i_48153++) {
        *(int32_t *) &mem_46874.mem[i_48153 * 4] = 0;
    }
    
    struct memblock mem_46877;
    
    mem_46877.references = NULL;
    if (memblock_alloc(ctx, &mem_46877, 4, "mem_46877"))
        return 1;
    for (int32_t i_48154 = 0; i_48154 < 1; i_48154++) {
        *(int32_t *) &mem_46877.mem[i_48154 * 4] = 4;
    }
    
    int32_t conc_tmp_43170 = 1 + last_offset_43142;
    int32_t res_43181;
    int32_t redout_45261 = last_offset_43142;
    
    for (int32_t i_45262 = 0; i_45262 < last_offset_43142; i_45262++) {
        int32_t x_43185 = *(int32_t *) &mem_46844.mem[i_45262 * 4];
        int32_t x_43186 = *(int32_t *) &mem_46847.mem[i_45262 * 4];
        bool cond_43188 = x_43185 == 1;
        bool cond_43189 = x_43186 == 0;
        bool eq_43190 = cond_43188 && cond_43189;
        int32_t res_43191;
        
        if (eq_43190) {
            res_43191 = i_45262;
        } else {
            res_43191 = last_offset_43142;
        }
        
        int32_t res_43184 = smin32(res_43191, redout_45261);
        int32_t redout_tmp_48155 = res_43184;
        
        redout_45261 = redout_tmp_48155;
    }
    res_43181 = redout_45261;
    
    bool cond_43192 = res_43181 == last_offset_43142;
    int32_t res_43193;
    
    if (cond_43192) {
        res_43193 = -1;
    } else {
        res_43193 = res_43181;
    }
    
    bool eq_x_zz_43194 = -1 == res_43181;
    bool not_p_43195 = !cond_43192;
    bool p_and_eq_x_y_43196 = eq_x_zz_43194 && not_p_43195;
    bool cond_43197 = cond_43192 || p_and_eq_x_y_43196;
    bool cond_43198 = !cond_43197;
    int32_t sizze_43199;
    
    if (cond_43198) {
        sizze_43199 = last_offset_43142;
    } else {
        sizze_43199 = conc_tmp_43170;
    }
    
    int64_t binop_x_46882 = sext_i32_i64(conc_tmp_43170);
    int64_t bytes_46881 = 4 * binop_x_46882;
    int64_t res_mem_sizze_46890;
    struct memblock res_mem_46891;
    
    res_mem_46891.references = NULL;
    
    int64_t res_mem_sizze_46892;
    struct memblock res_mem_46893;
    
    res_mem_46893.references = NULL;
    
    int64_t res_mem_sizze_46894;
    struct memblock res_mem_46895;
    
    res_mem_46895.references = NULL;
    if (cond_43198) {
        struct memblock mem_46880;
        
        mem_46880.references = NULL;
        if (memblock_alloc(ctx, &mem_46880, bytes_46842, "mem_46880"))
            return 1;
        memmove(mem_46880.mem + 0, mem_46850.mem + 0, last_offset_43142 *
                sizeof(int32_t));
        
        bool less_than_zzero_45265 = slt32(res_43193, 0);
        bool greater_than_sizze_45266 = sle32(last_offset_43142, res_43193);
        bool outside_bounds_dim_45267 = less_than_zzero_45265 ||
             greater_than_sizze_45266;
        
        if (!outside_bounds_dim_45267) {
            *(int32_t *) &mem_46880.mem[res_43193 * 4] = 4;
        }
        res_mem_sizze_46890 = bytes_46842;
        if (memblock_set(ctx, &res_mem_46891, &mem_46844, "mem_46844") != 0)
            return 1;
        res_mem_sizze_46892 = bytes_46842;
        if (memblock_set(ctx, &res_mem_46893, &mem_46847, "mem_46847") != 0)
            return 1;
        res_mem_sizze_46894 = bytes_46842;
        if (memblock_set(ctx, &res_mem_46895, &mem_46880, "mem_46880") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46880, "mem_46880") != 0)
            return 1;
    } else {
        struct memblock mem_46883;
        
        mem_46883.references = NULL;
        if (memblock_alloc(ctx, &mem_46883, bytes_46881, "mem_46883"))
            return 1;
        
        int32_t tmp_offs_48156 = 0;
        
        memmove(mem_46883.mem + tmp_offs_48156 * 4, mem_46844.mem + 0,
                last_offset_43142 * sizeof(int32_t));
        tmp_offs_48156 += last_offset_43142;
        memmove(mem_46883.mem + tmp_offs_48156 * 4, mem_46871.mem + 0,
                sizeof(int32_t));
        tmp_offs_48156 += 1;
        
        struct memblock mem_46886;
        
        mem_46886.references = NULL;
        if (memblock_alloc(ctx, &mem_46886, bytes_46881, "mem_46886"))
            return 1;
        
        int32_t tmp_offs_48157 = 0;
        
        memmove(mem_46886.mem + tmp_offs_48157 * 4, mem_46847.mem + 0,
                last_offset_43142 * sizeof(int32_t));
        tmp_offs_48157 += last_offset_43142;
        memmove(mem_46886.mem + tmp_offs_48157 * 4, mem_46874.mem + 0,
                sizeof(int32_t));
        tmp_offs_48157 += 1;
        
        struct memblock mem_46889;
        
        mem_46889.references = NULL;
        if (memblock_alloc(ctx, &mem_46889, bytes_46881, "mem_46889"))
            return 1;
        
        int32_t tmp_offs_48158 = 0;
        
        memmove(mem_46889.mem + tmp_offs_48158 * 4, mem_46850.mem + 0,
                last_offset_43142 * sizeof(int32_t));
        tmp_offs_48158 += last_offset_43142;
        memmove(mem_46889.mem + tmp_offs_48158 * 4, mem_46877.mem + 0,
                sizeof(int32_t));
        tmp_offs_48158 += 1;
        res_mem_sizze_46890 = bytes_46881;
        if (memblock_set(ctx, &res_mem_46891, &mem_46883, "mem_46883") != 0)
            return 1;
        res_mem_sizze_46892 = bytes_46881;
        if (memblock_set(ctx, &res_mem_46893, &mem_46886, "mem_46886") != 0)
            return 1;
        res_mem_sizze_46894 = bytes_46881;
        if (memblock_set(ctx, &res_mem_46895, &mem_46889, "mem_46889") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46889, "mem_46889") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46886, "mem_46886") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46883, "mem_46883") != 0)
            return 1;
    }
    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
        return 1;
    
    struct memblock mem_46898;
    
    mem_46898.references = NULL;
    if (memblock_alloc(ctx, &mem_46898, 12, "mem_46898"))
        return 1;
    
    struct memblock static_array_48159 = ctx->static_array_48159;
    
    memmove(mem_46898.mem + 0, static_array_48159.mem + 0, 3 * sizeof(int32_t));
    
    bool eq_x_y_43215 = 3 == last_offset_43142;
    bool eq_x_zz_43216 = 3 == conc_tmp_43170;
    bool p_and_eq_x_y_43217 = cond_43198 && eq_x_y_43215;
    bool p_and_eq_x_y_43218 = cond_43197 && eq_x_zz_43216;
    bool dim_eq_43219 = p_and_eq_x_y_43217 || p_and_eq_x_y_43218;
    bool arrays_equal_43220;
    
    if (dim_eq_43219) {
        bool all_equal_43222;
        bool redout_45271 = 1;
        
        for (int32_t i_45272 = 0; i_45272 < sizze_43199; i_45272++) {
            int32_t x_43226 = *(int32_t *) &res_mem_46895.mem[i_45272 * 4];
            int32_t y_43227 = *(int32_t *) &mem_46898.mem[i_45272 * 4];
            bool res_43228 = x_43226 == y_43227;
            bool res_43225 = res_43228 && redout_45271;
            bool redout_tmp_48160 = res_43225;
            
            redout_45271 = redout_tmp_48160;
        }
        all_equal_43222 = redout_45271;
        arrays_equal_43220 = all_equal_43222;
    } else {
        arrays_equal_43220 = 0;
    }
    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
        return 1;
    
    bool res_43229;
    
    if (arrays_equal_43220) {
        bool arrays_equal_43230;
        
        if (dim_eq_43219) {
            bool all_equal_43232;
            bool redout_45273 = 1;
            
            for (int32_t i_45274 = 0; i_45274 < sizze_43199; i_45274++) {
                int32_t x_43236 = *(int32_t *) &res_mem_46891.mem[i_45274 * 4];
                int32_t y_43237 = *(int32_t *) &mem_46699.mem[i_45274 * 4];
                bool res_43238 = x_43236 == y_43237;
                bool res_43235 = res_43238 && redout_45273;
                bool redout_tmp_48161 = res_43235;
                
                redout_45273 = redout_tmp_48161;
            }
            all_equal_43232 = redout_45273;
            arrays_equal_43230 = all_equal_43232;
        } else {
            arrays_equal_43230 = 0;
        }
        
        bool arrays_equal_43239;
        
        if (dim_eq_43219) {
            bool all_equal_43241;
            bool redout_45275 = 1;
            
            for (int32_t i_45276 = 0; i_45276 < sizze_43199; i_45276++) {
                int32_t x_43245 = *(int32_t *) &res_mem_46893.mem[i_45276 * 4];
                int32_t y_43246 = *(int32_t *) &mem_46702.mem[i_45276 * 4];
                bool res_43247 = x_43245 == y_43246;
                bool res_43244 = res_43247 && redout_45275;
                bool redout_tmp_48162 = res_43244;
                
                redout_45275 = redout_tmp_48162;
            }
            all_equal_43241 = redout_45275;
            arrays_equal_43239 = all_equal_43241;
        } else {
            arrays_equal_43239 = 0;
        }
        
        bool eq_43248 = arrays_equal_43230 && arrays_equal_43239;
        
        res_43229 = eq_43248;
    } else {
        res_43229 = 0;
    }
    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
        return 1;
    
    struct memblock mem_46901;
    
    mem_46901.references = NULL;
    if (memblock_alloc(ctx, &mem_46901, 16, "mem_46901"))
        return 1;
    
    struct memblock static_array_48163 = ctx->static_array_48163;
    
    memmove(mem_46901.mem + 0, static_array_48163.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_46904;
    
    mem_46904.references = NULL;
    if (memblock_alloc(ctx, &mem_46904, 16, "mem_46904"))
        return 1;
    
    struct memblock static_array_48164 = ctx->static_array_48164;
    
    memmove(mem_46904.mem + 0, static_array_48164.mem + 0, 4 * sizeof(int32_t));
    
    bool cond_43251;
    
    if (res_43229) {
        struct memblock mem_46907;
        
        mem_46907.references = NULL;
        if (memblock_alloc(ctx, &mem_46907, 16, "mem_46907"))
            return 1;
        
        struct memblock mem_46912;
        
        mem_46912.references = NULL;
        if (memblock_alloc(ctx, &mem_46912, 8, "mem_46912"))
            return 1;
        for (int32_t i_45279 = 0; i_45279 < 2; i_45279++) {
            for (int32_t i_48166 = 0; i_48166 < 2; i_48166++) {
                *(int32_t *) &mem_46912.mem[i_48166 * 4] = i_45279;
            }
            memmove(mem_46907.mem + 2 * i_45279 * 4, mem_46912.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_46912, "mem_46912") != 0)
            return 1;
        
        struct memblock mem_46917;
        
        mem_46917.references = NULL;
        if (memblock_alloc(ctx, &mem_46917, 16, "mem_46917"))
            return 1;
        
        struct memblock mem_46920;
        
        mem_46920.references = NULL;
        if (memblock_alloc(ctx, &mem_46920, 16, "mem_46920"))
            return 1;
        
        int32_t discard_45289;
        int32_t scanacc_45283 = 0;
        
        for (int32_t i_45286 = 0; i_45286 < 4; i_45286++) {
            bool not_arg_43262 = i_45286 == 0;
            bool res_43263 = !not_arg_43262;
            int32_t part_res_43264;
            
            if (res_43263) {
                part_res_43264 = 0;
            } else {
                part_res_43264 = 1;
            }
            
            int32_t part_res_43265;
            
            if (res_43263) {
                part_res_43265 = 1;
            } else {
                part_res_43265 = 0;
            }
            
            int32_t zz_43260 = part_res_43265 + scanacc_45283;
            
            *(int32_t *) &mem_46917.mem[i_45286 * 4] = zz_43260;
            *(int32_t *) &mem_46920.mem[i_45286 * 4] = part_res_43264;
            
            int32_t scanacc_tmp_48167 = zz_43260;
            
            scanacc_45283 = scanacc_tmp_48167;
        }
        discard_45289 = scanacc_45283;
        
        int32_t last_offset_43266 = *(int32_t *) &mem_46917.mem[12];
        int64_t binop_x_46930 = sext_i32_i64(last_offset_43266);
        int64_t bytes_46929 = 4 * binop_x_46930;
        struct memblock mem_46931;
        
        mem_46931.references = NULL;
        if (memblock_alloc(ctx, &mem_46931, bytes_46929, "mem_46931"))
            return 1;
        
        struct memblock mem_46934;
        
        mem_46934.references = NULL;
        if (memblock_alloc(ctx, &mem_46934, bytes_46929, "mem_46934"))
            return 1;
        
        struct memblock mem_46937;
        
        mem_46937.references = NULL;
        if (memblock_alloc(ctx, &mem_46937, bytes_46929, "mem_46937"))
            return 1;
        for (int32_t write_iter_45290 = 0; write_iter_45290 < 4;
             write_iter_45290++) {
            int32_t write_iv_45294 =
                    *(int32_t *) &mem_46920.mem[write_iter_45290 * 4];
            int32_t write_iv_45295 =
                    *(int32_t *) &mem_46917.mem[write_iter_45290 * 4];
            int32_t new_index_46252 = squot32(write_iter_45290, 2);
            int32_t binop_y_46254 = 2 * new_index_46252;
            int32_t new_index_46255 = write_iter_45290 - binop_y_46254;
            bool is_this_one_43278 = write_iv_45294 == 0;
            int32_t this_offset_43279 = -1 + write_iv_45295;
            int32_t total_res_43280;
            
            if (is_this_one_43278) {
                total_res_43280 = this_offset_43279;
            } else {
                total_res_43280 = -1;
            }
            
            bool less_than_zzero_45299 = slt32(total_res_43280, 0);
            bool greater_than_sizze_45300 = sle32(last_offset_43266,
                                                  total_res_43280);
            bool outside_bounds_dim_45301 = less_than_zzero_45299 ||
                 greater_than_sizze_45300;
            
            if (!outside_bounds_dim_45301) {
                memmove(mem_46931.mem + total_res_43280 * 4, mem_46907.mem +
                        (2 * new_index_46252 + new_index_46255) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45301) {
                struct memblock mem_46946;
                
                mem_46946.references = NULL;
                if (memblock_alloc(ctx, &mem_46946, 4, "mem_46946"))
                    return 1;
                
                int32_t x_48174;
                
                for (int32_t i_48173 = 0; i_48173 < 1; i_48173++) {
                    x_48174 = new_index_46255 + sext_i32_i32(i_48173);
                    *(int32_t *) &mem_46946.mem[i_48173 * 4] = x_48174;
                }
                memmove(mem_46934.mem + total_res_43280 * 4, mem_46946.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_46946, "mem_46946") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46946, "mem_46946") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45301) {
                struct memblock mem_46949;
                
                mem_46949.references = NULL;
                if (memblock_alloc(ctx, &mem_46949, 4, "mem_46949"))
                    return 1;
                
                int32_t x_48176;
                
                for (int32_t i_48175 = 0; i_48175 < 1; i_48175++) {
                    x_48176 = write_iter_45290 + sext_i32_i32(i_48175);
                    *(int32_t *) &mem_46949.mem[i_48175 * 4] = x_48176;
                }
                memmove(mem_46937.mem + total_res_43280 * 4, mem_46949.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_46949, "mem_46949") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46949, "mem_46949") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_46907, "mem_46907") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46917, "mem_46917") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46920, "mem_46920") != 0)
            return 1;
        
        int32_t conc_tmp_43291 = 1 + last_offset_43266;
        int32_t res_43302;
        int32_t redout_45317 = last_offset_43266;
        
        for (int32_t i_45318 = 0; i_45318 < last_offset_43266; i_45318++) {
            int32_t x_43306 = *(int32_t *) &mem_46931.mem[i_45318 * 4];
            int32_t x_43307 = *(int32_t *) &mem_46934.mem[i_45318 * 4];
            bool cond_43309 = x_43306 == 0;
            bool cond_43310 = x_43307 == 0;
            bool eq_43311 = cond_43309 && cond_43310;
            int32_t res_43312;
            
            if (eq_43311) {
                res_43312 = i_45318;
            } else {
                res_43312 = last_offset_43266;
            }
            
            int32_t res_43305 = smin32(res_43312, redout_45317);
            int32_t redout_tmp_48177 = res_43305;
            
            redout_45317 = redout_tmp_48177;
        }
        res_43302 = redout_45317;
        
        bool cond_43313 = res_43302 == last_offset_43266;
        int32_t res_43314;
        
        if (cond_43313) {
            res_43314 = -1;
        } else {
            res_43314 = res_43302;
        }
        
        bool eq_x_zz_43315 = -1 == res_43302;
        bool not_p_43316 = !cond_43313;
        bool p_and_eq_x_y_43317 = eq_x_zz_43315 && not_p_43316;
        bool cond_43318 = cond_43313 || p_and_eq_x_y_43317;
        bool cond_43319 = !cond_43318;
        int32_t sizze_43320;
        
        if (cond_43319) {
            sizze_43320 = last_offset_43266;
        } else {
            sizze_43320 = conc_tmp_43291;
        }
        
        int64_t binop_x_46960 = sext_i32_i64(conc_tmp_43291);
        int64_t bytes_46959 = 4 * binop_x_46960;
        int64_t res_mem_sizze_46968;
        struct memblock res_mem_46969;
        
        res_mem_46969.references = NULL;
        
        int64_t res_mem_sizze_46970;
        struct memblock res_mem_46971;
        
        res_mem_46971.references = NULL;
        
        int64_t res_mem_sizze_46972;
        struct memblock res_mem_46973;
        
        res_mem_46973.references = NULL;
        if (cond_43319) {
            struct memblock mem_46958;
            
            mem_46958.references = NULL;
            if (memblock_alloc(ctx, &mem_46958, bytes_46929, "mem_46958"))
                return 1;
            memmove(mem_46958.mem + 0, mem_46937.mem + 0, last_offset_43266 *
                    sizeof(int32_t));
            
            bool less_than_zzero_45321 = slt32(res_43314, 0);
            bool greater_than_sizze_45322 = sle32(last_offset_43266, res_43314);
            bool outside_bounds_dim_45323 = less_than_zzero_45321 ||
                 greater_than_sizze_45322;
            
            if (!outside_bounds_dim_45323) {
                *(int32_t *) &mem_46958.mem[res_43314 * 4] = 4;
            }
            res_mem_sizze_46968 = bytes_46929;
            if (memblock_set(ctx, &res_mem_46969, &mem_46931, "mem_46931") != 0)
                return 1;
            res_mem_sizze_46970 = bytes_46929;
            if (memblock_set(ctx, &res_mem_46971, &mem_46934, "mem_46934") != 0)
                return 1;
            res_mem_sizze_46972 = bytes_46929;
            if (memblock_set(ctx, &res_mem_46973, &mem_46958, "mem_46958") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46958, "mem_46958") != 0)
                return 1;
        } else {
            struct memblock mem_46961;
            
            mem_46961.references = NULL;
            if (memblock_alloc(ctx, &mem_46961, bytes_46959, "mem_46961"))
                return 1;
            
            int32_t tmp_offs_48178 = 0;
            
            memmove(mem_46961.mem + tmp_offs_48178 * 4, mem_46931.mem + 0,
                    last_offset_43266 * sizeof(int32_t));
            tmp_offs_48178 += last_offset_43266;
            memmove(mem_46961.mem + tmp_offs_48178 * 4, mem_46874.mem + 0,
                    sizeof(int32_t));
            tmp_offs_48178 += 1;
            
            struct memblock mem_46964;
            
            mem_46964.references = NULL;
            if (memblock_alloc(ctx, &mem_46964, bytes_46959, "mem_46964"))
                return 1;
            
            int32_t tmp_offs_48179 = 0;
            
            memmove(mem_46964.mem + tmp_offs_48179 * 4, mem_46934.mem + 0,
                    last_offset_43266 * sizeof(int32_t));
            tmp_offs_48179 += last_offset_43266;
            memmove(mem_46964.mem + tmp_offs_48179 * 4, mem_46874.mem + 0,
                    sizeof(int32_t));
            tmp_offs_48179 += 1;
            
            struct memblock mem_46967;
            
            mem_46967.references = NULL;
            if (memblock_alloc(ctx, &mem_46967, bytes_46959, "mem_46967"))
                return 1;
            
            int32_t tmp_offs_48180 = 0;
            
            memmove(mem_46967.mem + tmp_offs_48180 * 4, mem_46937.mem + 0,
                    last_offset_43266 * sizeof(int32_t));
            tmp_offs_48180 += last_offset_43266;
            memmove(mem_46967.mem + tmp_offs_48180 * 4, mem_46877.mem + 0,
                    sizeof(int32_t));
            tmp_offs_48180 += 1;
            res_mem_sizze_46968 = bytes_46959;
            if (memblock_set(ctx, &res_mem_46969, &mem_46961, "mem_46961") != 0)
                return 1;
            res_mem_sizze_46970 = bytes_46959;
            if (memblock_set(ctx, &res_mem_46971, &mem_46964, "mem_46964") != 0)
                return 1;
            res_mem_sizze_46972 = bytes_46959;
            if (memblock_set(ctx, &res_mem_46973, &mem_46967, "mem_46967") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46967, "mem_46967") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46964, "mem_46964") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46961, "mem_46961") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_46931, "mem_46931") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46934, "mem_46934") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46937, "mem_46937") != 0)
            return 1;
        
        bool eq_x_y_43335 = 4 == last_offset_43266;
        bool eq_x_zz_43336 = 4 == conc_tmp_43291;
        bool p_and_eq_x_y_43337 = cond_43319 && eq_x_y_43335;
        bool p_and_eq_x_y_43338 = cond_43318 && eq_x_zz_43336;
        bool dim_eq_43339 = p_and_eq_x_y_43337 || p_and_eq_x_y_43338;
        bool arrays_equal_43340;
        
        if (dim_eq_43339) {
            bool all_equal_43342;
            bool redout_45327 = 1;
            
            for (int32_t i_45328 = 0; i_45328 < sizze_43320; i_45328++) {
                int32_t x_43346 = *(int32_t *) &res_mem_46973.mem[i_45328 * 4];
                int32_t y_43347 = *(int32_t *) &mem_46642.mem[i_45328 * 4];
                bool res_43348 = x_43346 == y_43347;
                bool res_43345 = res_43348 && redout_45327;
                bool redout_tmp_48181 = res_43345;
                
                redout_45327 = redout_tmp_48181;
            }
            all_equal_43342 = redout_45327;
            arrays_equal_43340 = all_equal_43342;
        } else {
            arrays_equal_43340 = 0;
        }
        if (memblock_unref(ctx, &res_mem_46973, "res_mem_46973") != 0)
            return 1;
        
        bool res_43349;
        
        if (arrays_equal_43340) {
            bool arrays_equal_43350;
            
            if (dim_eq_43339) {
                bool all_equal_43352;
                bool redout_45329 = 1;
                
                for (int32_t i_45330 = 0; i_45330 < sizze_43320; i_45330++) {
                    int32_t x_43356 = *(int32_t *) &res_mem_46969.mem[i_45330 *
                                                                      4];
                    int32_t y_43357 = *(int32_t *) &mem_46901.mem[i_45330 * 4];
                    bool res_43358 = x_43356 == y_43357;
                    bool res_43355 = res_43358 && redout_45329;
                    bool redout_tmp_48182 = res_43355;
                    
                    redout_45329 = redout_tmp_48182;
                }
                all_equal_43352 = redout_45329;
                arrays_equal_43350 = all_equal_43352;
            } else {
                arrays_equal_43350 = 0;
            }
            
            bool arrays_equal_43359;
            
            if (dim_eq_43339) {
                bool all_equal_43361;
                bool redout_45331 = 1;
                
                for (int32_t i_45332 = 0; i_45332 < sizze_43320; i_45332++) {
                    int32_t x_43365 = *(int32_t *) &res_mem_46971.mem[i_45332 *
                                                                      4];
                    int32_t y_43366 = *(int32_t *) &mem_46904.mem[i_45332 * 4];
                    bool res_43367 = x_43365 == y_43366;
                    bool res_43364 = res_43367 && redout_45331;
                    bool redout_tmp_48183 = res_43364;
                    
                    redout_45331 = redout_tmp_48183;
                }
                all_equal_43361 = redout_45331;
                arrays_equal_43359 = all_equal_43361;
            } else {
                arrays_equal_43359 = 0;
            }
            
            bool eq_43368 = arrays_equal_43350 && arrays_equal_43359;
            
            res_43349 = eq_43368;
        } else {
            res_43349 = 0;
        }
        if (memblock_unref(ctx, &res_mem_46969, "res_mem_46969") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46971, "res_mem_46971") != 0)
            return 1;
        cond_43251 = res_43349;
        if (memblock_unref(ctx, &res_mem_46973, "res_mem_46973") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46971, "res_mem_46971") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46969, "res_mem_46969") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46937, "mem_46937") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46934, "mem_46934") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46931, "mem_46931") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46920, "mem_46920") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46917, "mem_46917") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46912, "mem_46912") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46907, "mem_46907") != 0)
            return 1;
    } else {
        cond_43251 = 0;
    }
    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
        return 1;
    
    bool res_43369;
    
    if (cond_43251) {
        struct memblock mem_46976;
        
        mem_46976.references = NULL;
        if (memblock_alloc(ctx, &mem_46976, 16, "mem_46976"))
            return 1;
        
        struct memblock mem_46981;
        
        mem_46981.references = NULL;
        if (memblock_alloc(ctx, &mem_46981, 8, "mem_46981"))
            return 1;
        for (int32_t i_45335 = 0; i_45335 < 2; i_45335++) {
            for (int32_t i_48185 = 0; i_48185 < 2; i_48185++) {
                *(int32_t *) &mem_46981.mem[i_48185 * 4] = i_45335;
            }
            memmove(mem_46976.mem + 2 * i_45335 * 4, mem_46981.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_46981, "mem_46981") != 0)
            return 1;
        
        struct memblock mem_46986;
        
        mem_46986.references = NULL;
        if (memblock_alloc(ctx, &mem_46986, 16, "mem_46986"))
            return 1;
        
        struct memblock mem_46989;
        
        mem_46989.references = NULL;
        if (memblock_alloc(ctx, &mem_46989, 16, "mem_46989"))
            return 1;
        
        int32_t discard_45345;
        int32_t scanacc_45339 = 0;
        
        for (int32_t i_45342 = 0; i_45342 < 4; i_45342++) {
            bool not_arg_43380 = i_45342 == 0;
            bool res_43381 = !not_arg_43380;
            int32_t part_res_43382;
            
            if (res_43381) {
                part_res_43382 = 0;
            } else {
                part_res_43382 = 1;
            }
            
            int32_t part_res_43383;
            
            if (res_43381) {
                part_res_43383 = 1;
            } else {
                part_res_43383 = 0;
            }
            
            int32_t zz_43378 = part_res_43383 + scanacc_45339;
            
            *(int32_t *) &mem_46986.mem[i_45342 * 4] = zz_43378;
            *(int32_t *) &mem_46989.mem[i_45342 * 4] = part_res_43382;
            
            int32_t scanacc_tmp_48186 = zz_43378;
            
            scanacc_45339 = scanacc_tmp_48186;
        }
        discard_45345 = scanacc_45339;
        
        int32_t last_offset_43384 = *(int32_t *) &mem_46986.mem[12];
        int64_t binop_x_46999 = sext_i32_i64(last_offset_43384);
        int64_t bytes_46998 = 4 * binop_x_46999;
        struct memblock mem_47000;
        
        mem_47000.references = NULL;
        if (memblock_alloc(ctx, &mem_47000, bytes_46998, "mem_47000"))
            return 1;
        
        struct memblock mem_47003;
        
        mem_47003.references = NULL;
        if (memblock_alloc(ctx, &mem_47003, bytes_46998, "mem_47003"))
            return 1;
        
        struct memblock mem_47006;
        
        mem_47006.references = NULL;
        if (memblock_alloc(ctx, &mem_47006, bytes_46998, "mem_47006"))
            return 1;
        for (int32_t write_iter_45346 = 0; write_iter_45346 < 4;
             write_iter_45346++) {
            int32_t write_iv_45350 =
                    *(int32_t *) &mem_46989.mem[write_iter_45346 * 4];
            int32_t write_iv_45351 =
                    *(int32_t *) &mem_46986.mem[write_iter_45346 * 4];
            int32_t new_index_46272 = squot32(write_iter_45346, 2);
            int32_t binop_y_46274 = 2 * new_index_46272;
            int32_t new_index_46275 = write_iter_45346 - binop_y_46274;
            bool is_this_one_43396 = write_iv_45350 == 0;
            int32_t this_offset_43397 = -1 + write_iv_45351;
            int32_t total_res_43398;
            
            if (is_this_one_43396) {
                total_res_43398 = this_offset_43397;
            } else {
                total_res_43398 = -1;
            }
            
            bool less_than_zzero_45355 = slt32(total_res_43398, 0);
            bool greater_than_sizze_45356 = sle32(last_offset_43384,
                                                  total_res_43398);
            bool outside_bounds_dim_45357 = less_than_zzero_45355 ||
                 greater_than_sizze_45356;
            
            if (!outside_bounds_dim_45357) {
                memmove(mem_47000.mem + total_res_43398 * 4, mem_46976.mem +
                        (2 * new_index_46272 + new_index_46275) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45357) {
                struct memblock mem_47015;
                
                mem_47015.references = NULL;
                if (memblock_alloc(ctx, &mem_47015, 4, "mem_47015"))
                    return 1;
                
                int32_t x_48193;
                
                for (int32_t i_48192 = 0; i_48192 < 1; i_48192++) {
                    x_48193 = new_index_46275 + sext_i32_i32(i_48192);
                    *(int32_t *) &mem_47015.mem[i_48192 * 4] = x_48193;
                }
                memmove(mem_47003.mem + total_res_43398 * 4, mem_47015.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47015, "mem_47015") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47015, "mem_47015") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45357) {
                struct memblock mem_47018;
                
                mem_47018.references = NULL;
                if (memblock_alloc(ctx, &mem_47018, 4, "mem_47018"))
                    return 1;
                
                int32_t x_48195;
                
                for (int32_t i_48194 = 0; i_48194 < 1; i_48194++) {
                    x_48195 = write_iter_45346 + sext_i32_i32(i_48194);
                    *(int32_t *) &mem_47018.mem[i_48194 * 4] = x_48195;
                }
                memmove(mem_47006.mem + total_res_43398 * 4, mem_47018.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47018, "mem_47018") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47018, "mem_47018") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_46976, "mem_46976") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46986, "mem_46986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46989, "mem_46989") != 0)
            return 1;
        
        int32_t res_43419;
        int32_t redout_45373 = last_offset_43384;
        
        for (int32_t i_45374 = 0; i_45374 < last_offset_43384; i_45374++) {
            int32_t x_43423 = *(int32_t *) &mem_47000.mem[i_45374 * 4];
            int32_t x_43424 = *(int32_t *) &mem_47003.mem[i_45374 * 4];
            bool cond_43426 = x_43423 == 0;
            bool cond_43427 = x_43424 == 1;
            bool eq_43428 = cond_43426 && cond_43427;
            int32_t res_43429;
            
            if (eq_43428) {
                res_43429 = i_45374;
            } else {
                res_43429 = last_offset_43384;
            }
            
            int32_t res_43422 = smin32(res_43429, redout_45373);
            int32_t redout_tmp_48196 = res_43422;
            
            redout_45373 = redout_tmp_48196;
        }
        res_43419 = redout_45373;
        if (memblock_unref(ctx, &mem_47000, "mem_47000") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47003, "mem_47003") != 0)
            return 1;
        
        bool cond_43430 = res_43419 == last_offset_43384;
        int32_t res_43431;
        
        if (cond_43430) {
            res_43431 = -1;
        } else {
            res_43431 = res_43419;
        }
        
        bool eq_x_zz_43432 = -1 == res_43419;
        bool not_p_43433 = !cond_43430;
        bool p_and_eq_x_y_43434 = eq_x_zz_43432 && not_p_43433;
        bool cond_43435 = cond_43430 || p_and_eq_x_y_43434;
        int32_t res_43436;
        
        if (cond_43435) {
            res_43436 = 0;
        } else {
            int32_t res_43437 = *(int32_t *) &mem_47006.mem[res_43431 * 4];
            
            res_43436 = res_43437;
        }
        if (memblock_unref(ctx, &mem_47006, "mem_47006") != 0)
            return 1;
        
        bool res_43438 = res_43436 == 1;
        
        res_43369 = res_43438;
        if (memblock_unref(ctx, &mem_47006, "mem_47006") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47003, "mem_47003") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47000, "mem_47000") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46989, "mem_46989") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46986, "mem_46986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46981, "mem_46981") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46976, "mem_46976") != 0)
            return 1;
    } else {
        res_43369 = 0;
    }
    
    struct memblock mem_47027;
    
    mem_47027.references = NULL;
    if (memblock_alloc(ctx, &mem_47027, 16, "mem_47027"))
        return 1;
    
    struct memblock mem_47032;
    
    mem_47032.references = NULL;
    if (memblock_alloc(ctx, &mem_47032, 8, "mem_47032"))
        return 1;
    for (int32_t i_45377 = 0; i_45377 < 2; i_45377++) {
        for (int32_t i_48198 = 0; i_48198 < 2; i_48198++) {
            *(int32_t *) &mem_47032.mem[i_48198 * 4] = i_45377;
        }
        memmove(mem_47027.mem + 2 * i_45377 * 4, mem_47032.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
        return 1;
    
    struct memblock mem_47037;
    
    mem_47037.references = NULL;
    if (memblock_alloc(ctx, &mem_47037, 16, "mem_47037"))
        return 1;
    
    struct memblock mem_47040;
    
    mem_47040.references = NULL;
    if (memblock_alloc(ctx, &mem_47040, 16, "mem_47040"))
        return 1;
    
    int32_t discard_45387;
    int32_t scanacc_45381 = 0;
    
    for (int32_t i_45384 = 0; i_45384 < 4; i_45384++) {
        bool not_arg_43449 = i_45384 == 0;
        bool res_43450 = !not_arg_43449;
        int32_t part_res_43451;
        
        if (res_43450) {
            part_res_43451 = 0;
        } else {
            part_res_43451 = 1;
        }
        
        int32_t part_res_43452;
        
        if (res_43450) {
            part_res_43452 = 1;
        } else {
            part_res_43452 = 0;
        }
        
        int32_t zz_43447 = part_res_43452 + scanacc_45381;
        
        *(int32_t *) &mem_47037.mem[i_45384 * 4] = zz_43447;
        *(int32_t *) &mem_47040.mem[i_45384 * 4] = part_res_43451;
        
        int32_t scanacc_tmp_48199 = zz_43447;
        
        scanacc_45381 = scanacc_tmp_48199;
    }
    discard_45387 = scanacc_45381;
    
    int32_t last_offset_43453 = *(int32_t *) &mem_47037.mem[12];
    int64_t binop_x_47050 = sext_i32_i64(last_offset_43453);
    int64_t bytes_47049 = 4 * binop_x_47050;
    struct memblock mem_47051;
    
    mem_47051.references = NULL;
    if (memblock_alloc(ctx, &mem_47051, bytes_47049, "mem_47051"))
        return 1;
    
    struct memblock mem_47054;
    
    mem_47054.references = NULL;
    if (memblock_alloc(ctx, &mem_47054, bytes_47049, "mem_47054"))
        return 1;
    
    struct memblock mem_47057;
    
    mem_47057.references = NULL;
    if (memblock_alloc(ctx, &mem_47057, bytes_47049, "mem_47057"))
        return 1;
    for (int32_t write_iter_45388 = 0; write_iter_45388 < 4;
         write_iter_45388++) {
        int32_t write_iv_45392 = *(int32_t *) &mem_47040.mem[write_iter_45388 *
                                                             4];
        int32_t write_iv_45393 = *(int32_t *) &mem_47037.mem[write_iter_45388 *
                                                             4];
        int32_t new_index_46292 = squot32(write_iter_45388, 2);
        int32_t binop_y_46294 = 2 * new_index_46292;
        int32_t new_index_46295 = write_iter_45388 - binop_y_46294;
        bool is_this_one_43465 = write_iv_45392 == 0;
        int32_t this_offset_43466 = -1 + write_iv_45393;
        int32_t total_res_43467;
        
        if (is_this_one_43465) {
            total_res_43467 = this_offset_43466;
        } else {
            total_res_43467 = -1;
        }
        
        bool less_than_zzero_45397 = slt32(total_res_43467, 0);
        bool greater_than_sizze_45398 = sle32(last_offset_43453,
                                              total_res_43467);
        bool outside_bounds_dim_45399 = less_than_zzero_45397 ||
             greater_than_sizze_45398;
        
        if (!outside_bounds_dim_45399) {
            memmove(mem_47051.mem + total_res_43467 * 4, mem_47027.mem + (2 *
                                                                          new_index_46292 +
                                                                          new_index_46295) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_45399) {
            struct memblock mem_47066;
            
            mem_47066.references = NULL;
            if (memblock_alloc(ctx, &mem_47066, 4, "mem_47066"))
                return 1;
            
            int32_t x_48206;
            
            for (int32_t i_48205 = 0; i_48205 < 1; i_48205++) {
                x_48206 = new_index_46295 + sext_i32_i32(i_48205);
                *(int32_t *) &mem_47066.mem[i_48205 * 4] = x_48206;
            }
            memmove(mem_47054.mem + total_res_43467 * 4, mem_47066.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_47066, "mem_47066") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47066, "mem_47066") != 0)
                return 1;
        }
        if (!outside_bounds_dim_45399) {
            struct memblock mem_47069;
            
            mem_47069.references = NULL;
            if (memblock_alloc(ctx, &mem_47069, 4, "mem_47069"))
                return 1;
            
            int32_t x_48208;
            
            for (int32_t i_48207 = 0; i_48207 < 1; i_48207++) {
                x_48208 = write_iter_45388 + sext_i32_i32(i_48207);
                *(int32_t *) &mem_47069.mem[i_48207 * 4] = x_48208;
            }
            memmove(mem_47057.mem + total_res_43467 * 4, mem_47069.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_47069, "mem_47069") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47069, "mem_47069") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
        return 1;
    
    struct memblock mem_47078;
    
    mem_47078.references = NULL;
    if (memblock_alloc(ctx, &mem_47078, 16, "mem_47078"))
        return 1;
    for (int32_t i_48209 = 0; i_48209 < 4; i_48209++) {
        *(int32_t *) &mem_47078.mem[i_48209 * 4] = 0;
    }
    for (int32_t write_iter_45415 = 0; write_iter_45415 < last_offset_43453;
         write_iter_45415++) {
        int32_t write_iv_45417 = *(int32_t *) &mem_47054.mem[write_iter_45415 *
                                                             4];
        int32_t write_iv_45418 = *(int32_t *) &mem_47051.mem[write_iter_45415 *
                                                             4];
        int32_t x_43473 = 2 * write_iv_45417;
        int32_t res_43474 = x_43473 + write_iv_45418;
        bool less_than_zzero_45420 = slt32(res_43474, 0);
        bool greater_than_sizze_45421 = sle32(4, res_43474);
        bool outside_bounds_dim_45422 = less_than_zzero_45420 ||
             greater_than_sizze_45421;
        
        if (!outside_bounds_dim_45422) {
            memmove(mem_47078.mem + res_43474 * 4, mem_47057.mem +
                    write_iter_45415 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
        return 1;
    
    bool res_43475;
    bool redout_45426 = 1;
    
    for (int32_t i_45427 = 0; i_45427 < 2; i_45427++) {
        int32_t binop_x_43480 = 2 * i_45427;
        int32_t new_index_43481 = binop_x_43480 + i_45427;
        int32_t y_43482 = *(int32_t *) &mem_47078.mem[new_index_43481 * 4];
        bool res_43483 = new_index_43481 == y_43482;
        bool x_43478 = res_43483 && redout_45426;
        bool redout_tmp_48211 = x_43478;
        
        redout_45426 = redout_tmp_48211;
    }
    res_43475 = redout_45426;
    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
        return 1;
    
    bool cond_43484;
    
    if (res_43475) {
        struct memblock mem_47085;
        
        mem_47085.references = NULL;
        if (memblock_alloc(ctx, &mem_47085, 16, "mem_47085"))
            return 1;
        
        struct memblock mem_47090;
        
        mem_47090.references = NULL;
        if (memblock_alloc(ctx, &mem_47090, 8, "mem_47090"))
            return 1;
        for (int32_t i_45430 = 0; i_45430 < 2; i_45430++) {
            for (int32_t i_48213 = 0; i_48213 < 2; i_48213++) {
                *(int32_t *) &mem_47090.mem[i_48213 * 4] = i_45430;
            }
            memmove(mem_47085.mem + 2 * i_45430 * 4, mem_47090.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_47090, "mem_47090") != 0)
            return 1;
        
        struct memblock mem_47095;
        
        mem_47095.references = NULL;
        if (memblock_alloc(ctx, &mem_47095, 16, "mem_47095"))
            return 1;
        
        struct memblock mem_47098;
        
        mem_47098.references = NULL;
        if (memblock_alloc(ctx, &mem_47098, 16, "mem_47098"))
            return 1;
        
        int32_t discard_45440;
        int32_t scanacc_45434 = 0;
        
        for (int32_t i_45437 = 0; i_45437 < 4; i_45437++) {
            bool not_arg_43495 = i_45437 == 0;
            bool res_43496 = !not_arg_43495;
            int32_t part_res_43497;
            
            if (res_43496) {
                part_res_43497 = 0;
            } else {
                part_res_43497 = 1;
            }
            
            int32_t part_res_43498;
            
            if (res_43496) {
                part_res_43498 = 1;
            } else {
                part_res_43498 = 0;
            }
            
            int32_t zz_43493 = part_res_43498 + scanacc_45434;
            
            *(int32_t *) &mem_47095.mem[i_45437 * 4] = zz_43493;
            *(int32_t *) &mem_47098.mem[i_45437 * 4] = part_res_43497;
            
            int32_t scanacc_tmp_48214 = zz_43493;
            
            scanacc_45434 = scanacc_tmp_48214;
        }
        discard_45440 = scanacc_45434;
        
        int32_t last_offset_43499 = *(int32_t *) &mem_47095.mem[12];
        int64_t binop_x_47108 = sext_i32_i64(last_offset_43499);
        int64_t bytes_47107 = 4 * binop_x_47108;
        struct memblock mem_47109;
        
        mem_47109.references = NULL;
        if (memblock_alloc(ctx, &mem_47109, bytes_47107, "mem_47109"))
            return 1;
        
        struct memblock mem_47112;
        
        mem_47112.references = NULL;
        if (memblock_alloc(ctx, &mem_47112, bytes_47107, "mem_47112"))
            return 1;
        
        struct memblock mem_47115;
        
        mem_47115.references = NULL;
        if (memblock_alloc(ctx, &mem_47115, bytes_47107, "mem_47115"))
            return 1;
        for (int32_t write_iter_45441 = 0; write_iter_45441 < 4;
             write_iter_45441++) {
            int32_t write_iv_45445 =
                    *(int32_t *) &mem_47098.mem[write_iter_45441 * 4];
            int32_t write_iv_45446 =
                    *(int32_t *) &mem_47095.mem[write_iter_45441 * 4];
            int32_t new_index_46313 = squot32(write_iter_45441, 2);
            int32_t binop_y_46315 = 2 * new_index_46313;
            int32_t new_index_46316 = write_iter_45441 - binop_y_46315;
            bool is_this_one_43511 = write_iv_45445 == 0;
            int32_t this_offset_43512 = -1 + write_iv_45446;
            int32_t total_res_43513;
            
            if (is_this_one_43511) {
                total_res_43513 = this_offset_43512;
            } else {
                total_res_43513 = -1;
            }
            
            bool less_than_zzero_45450 = slt32(total_res_43513, 0);
            bool greater_than_sizze_45451 = sle32(last_offset_43499,
                                                  total_res_43513);
            bool outside_bounds_dim_45452 = less_than_zzero_45450 ||
                 greater_than_sizze_45451;
            
            if (!outside_bounds_dim_45452) {
                memmove(mem_47109.mem + total_res_43513 * 4, mem_47085.mem +
                        (2 * new_index_46313 + new_index_46316) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45452) {
                struct memblock mem_47124;
                
                mem_47124.references = NULL;
                if (memblock_alloc(ctx, &mem_47124, 4, "mem_47124"))
                    return 1;
                
                int32_t x_48221;
                
                for (int32_t i_48220 = 0; i_48220 < 1; i_48220++) {
                    x_48221 = new_index_46316 + sext_i32_i32(i_48220);
                    *(int32_t *) &mem_47124.mem[i_48220 * 4] = x_48221;
                }
                memmove(mem_47112.mem + total_res_43513 * 4, mem_47124.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47124, "mem_47124") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47124, "mem_47124") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45452) {
                struct memblock mem_47127;
                
                mem_47127.references = NULL;
                if (memblock_alloc(ctx, &mem_47127, 4, "mem_47127"))
                    return 1;
                
                int32_t x_48223;
                
                for (int32_t i_48222 = 0; i_48222 < 1; i_48222++) {
                    x_48223 = write_iter_45441 + sext_i32_i32(i_48222);
                    *(int32_t *) &mem_47127.mem[i_48222 * 4] = x_48223;
                }
                memmove(mem_47115.mem + total_res_43513 * 4, mem_47127.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47127, "mem_47127") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47127, "mem_47127") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_47085, "mem_47085") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47095, "mem_47095") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47098, "mem_47098") != 0)
            return 1;
        
        struct memblock mem_47136;
        
        mem_47136.references = NULL;
        if (memblock_alloc(ctx, &mem_47136, 16, "mem_47136"))
            return 1;
        for (int32_t i_48224 = 0; i_48224 < 4; i_48224++) {
            *(int32_t *) &mem_47136.mem[i_48224 * 4] = 0;
        }
        for (int32_t write_iter_45468 = 0; write_iter_45468 < last_offset_43499;
             write_iter_45468++) {
            int32_t write_iv_45470 =
                    *(int32_t *) &mem_47109.mem[write_iter_45468 * 4];
            int32_t write_iv_45471 =
                    *(int32_t *) &mem_47112.mem[write_iter_45468 * 4];
            int32_t x_43519 = 2 * write_iv_45470;
            int32_t res_43520 = x_43519 + write_iv_45471;
            bool less_than_zzero_45473 = slt32(res_43520, 0);
            bool greater_than_sizze_45474 = sle32(4, res_43520);
            bool outside_bounds_dim_45475 = less_than_zzero_45473 ||
                 greater_than_sizze_45474;
            
            if (!outside_bounds_dim_45475) {
                memmove(mem_47136.mem + res_43520 * 4, mem_47115.mem +
                        write_iter_45468 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47109, "mem_47109") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47112, "mem_47112") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47115, "mem_47115") != 0)
            return 1;
        
        bool all_equal_43521;
        bool redout_45479 = 1;
        
        for (int32_t i_45480 = 0; i_45480 < 4; i_45480++) {
            int32_t x_43525 = *(int32_t *) &mem_47136.mem[i_45480 * 4];
            bool res_43527 = x_43525 == i_45480;
            bool res_43524 = res_43527 && redout_45479;
            bool redout_tmp_48226 = res_43524;
            
            redout_45479 = redout_tmp_48226;
        }
        all_equal_43521 = redout_45479;
        if (memblock_unref(ctx, &mem_47136, "mem_47136") != 0)
            return 1;
        cond_43484 = all_equal_43521;
        if (memblock_unref(ctx, &mem_47136, "mem_47136") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47115, "mem_47115") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47112, "mem_47112") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47109, "mem_47109") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47098, "mem_47098") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47095, "mem_47095") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47090, "mem_47090") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47085, "mem_47085") != 0)
            return 1;
    } else {
        cond_43484 = 0;
    }
    
    bool res_43528;
    
    if (cond_43484) {
        struct memblock mem_47143;
        
        mem_47143.references = NULL;
        if (memblock_alloc(ctx, &mem_47143, 36, "mem_47143"))
            return 1;
        for (int32_t i_48227 = 0; i_48227 < 9; i_48227++) {
            *(int32_t *) &mem_47143.mem[i_48227 * 4] = 0;
        }
        
        struct memblock mem_47146;
        
        mem_47146.references = NULL;
        if (memblock_alloc(ctx, &mem_47146, 36, "mem_47146"))
            return 1;
        for (int32_t i_48228 = 0; i_48228 < 9; i_48228++) {
            *(int32_t *) &mem_47146.mem[i_48228 * 4] = 0;
        }
        for (int32_t write_iter_45481 = 0; write_iter_45481 < 3;
             write_iter_45481++) {
            int32_t x_43535 = 3 * write_iter_45481;
            int32_t res_43536 = x_43535 + write_iter_45481;
            bool less_than_zzero_45485 = slt32(res_43536, 0);
            bool greater_than_sizze_45486 = sle32(9, res_43536);
            bool outside_bounds_dim_45487 = less_than_zzero_45485 ||
                 greater_than_sizze_45486;
            
            if (!outside_bounds_dim_45487) {
                *(int32_t *) &mem_47146.mem[res_43536 * 4] = 1;
            }
            if (!outside_bounds_dim_45487) {
                *(int32_t *) &mem_47143.mem[res_43536 * 4] = 1;
            }
        }
        
        bool all_equal_43537;
        bool redout_45497 = 1;
        
        for (int32_t i_45498 = 0; i_45498 < 9; i_45498++) {
            int32_t x_43541 = *(int32_t *) &mem_47146.mem[i_45498 * 4];
            int32_t y_43542 = *(int32_t *) &mem_47143.mem[i_45498 * 4];
            bool res_43543 = x_43541 == y_43542;
            bool res_43540 = res_43543 && redout_45497;
            bool redout_tmp_48231 = res_43540;
            
            redout_45497 = redout_tmp_48231;
        }
        all_equal_43537 = redout_45497;
        if (memblock_unref(ctx, &mem_47143, "mem_47143") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47146, "mem_47146") != 0)
            return 1;
        res_43528 = all_equal_43537;
        if (memblock_unref(ctx, &mem_47146, "mem_47146") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47143, "mem_47143") != 0)
            return 1;
    } else {
        res_43528 = 0;
    }
    
    struct memblock mem_47157;
    
    mem_47157.references = NULL;
    if (memblock_alloc(ctx, &mem_47157, 16, "mem_47157"))
        return 1;
    
    struct memblock mem_47162;
    
    mem_47162.references = NULL;
    if (memblock_alloc(ctx, &mem_47162, 8, "mem_47162"))
        return 1;
    for (int32_t i_45506 = 0; i_45506 < 2; i_45506++) {
        for (int32_t i_48233 = 0; i_48233 < 2; i_48233++) {
            *(int32_t *) &mem_47162.mem[i_48233 * 4] = i_45506;
        }
        memmove(mem_47157.mem + 2 * i_45506 * 4, mem_47162.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
        return 1;
    
    struct memblock mem_47167;
    
    mem_47167.references = NULL;
    if (memblock_alloc(ctx, &mem_47167, 16, "mem_47167"))
        return 1;
    
    int32_t discard_45513;
    int32_t scanacc_45509 = 0;
    
    for (int32_t i_45511 = 0; i_45511 < 4; i_45511++) {
        int32_t zz_43570 = 1 + scanacc_45509;
        
        *(int32_t *) &mem_47167.mem[i_45511 * 4] = zz_43570;
        
        int32_t scanacc_tmp_48234 = zz_43570;
        
        scanacc_45509 = scanacc_tmp_48234;
    }
    discard_45513 = scanacc_45509;
    
    int32_t last_offset_43572 = *(int32_t *) &mem_47167.mem[12];
    int64_t binop_x_47173 = sext_i32_i64(last_offset_43572);
    int64_t bytes_47172 = 4 * binop_x_47173;
    struct memblock mem_47174;
    
    mem_47174.references = NULL;
    if (memblock_alloc(ctx, &mem_47174, bytes_47172, "mem_47174"))
        return 1;
    
    struct memblock mem_47177;
    
    mem_47177.references = NULL;
    if (memblock_alloc(ctx, &mem_47177, bytes_47172, "mem_47177"))
        return 1;
    
    struct memblock mem_47180;
    
    mem_47180.references = NULL;
    if (memblock_alloc(ctx, &mem_47180, bytes_47172, "mem_47180"))
        return 1;
    for (int32_t write_iter_45514 = 0; write_iter_45514 < 4;
         write_iter_45514++) {
        int32_t write_iv_45518 = *(int32_t *) &mem_47167.mem[write_iter_45514 *
                                                             4];
        int32_t new_index_46334 = squot32(write_iter_45514, 2);
        int32_t binop_y_46336 = 2 * new_index_46334;
        int32_t new_index_46337 = write_iter_45514 - binop_y_46336;
        int32_t this_offset_43582 = -1 + write_iv_45518;
        bool less_than_zzero_45521 = slt32(this_offset_43582, 0);
        bool greater_than_sizze_45522 = sle32(last_offset_43572,
                                              this_offset_43582);
        bool outside_bounds_dim_45523 = less_than_zzero_45521 ||
             greater_than_sizze_45522;
        
        if (!outside_bounds_dim_45523) {
            memmove(mem_47174.mem + this_offset_43582 * 4, mem_47157.mem + (2 *
                                                                            new_index_46334 +
                                                                            new_index_46337) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_45523) {
            struct memblock mem_47189;
            
            mem_47189.references = NULL;
            if (memblock_alloc(ctx, &mem_47189, 4, "mem_47189"))
                return 1;
            
            int32_t x_48240;
            
            for (int32_t i_48239 = 0; i_48239 < 1; i_48239++) {
                x_48240 = new_index_46337 + sext_i32_i32(i_48239);
                *(int32_t *) &mem_47189.mem[i_48239 * 4] = x_48240;
            }
            memmove(mem_47177.mem + this_offset_43582 * 4, mem_47189.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_47189, "mem_47189") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47189, "mem_47189") != 0)
                return 1;
        }
        if (!outside_bounds_dim_45523) {
            *(int32_t *) &mem_47180.mem[this_offset_43582 * 4] = 1;
        }
    }
    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
        return 1;
    
    struct memblock mem_47198;
    
    mem_47198.references = NULL;
    if (memblock_alloc(ctx, &mem_47198, 16, "mem_47198"))
        return 1;
    for (int32_t i_48241 = 0; i_48241 < 4; i_48241++) {
        *(int32_t *) &mem_47198.mem[i_48241 * 4] = 0;
    }
    for (int32_t write_iter_45539 = 0; write_iter_45539 < last_offset_43572;
         write_iter_45539++) {
        int32_t write_iv_45541 = *(int32_t *) &mem_47180.mem[write_iter_45539 *
                                                             4];
        int32_t write_iv_45542 = *(int32_t *) &mem_47174.mem[write_iter_45539 *
                                                             4];
        int32_t write_iv_45543 = *(int32_t *) &mem_47177.mem[write_iter_45539 *
                                                             4];
        int32_t res_43588 = 2 * write_iv_45541;
        int32_t x_43589 = 2 * write_iv_45542;
        int32_t res_43590 = x_43589 + write_iv_45543;
        bool less_than_zzero_45544 = slt32(res_43590, 0);
        bool greater_than_sizze_45545 = sle32(4, res_43590);
        bool outside_bounds_dim_45546 = less_than_zzero_45544 ||
             greater_than_sizze_45545;
        
        if (!outside_bounds_dim_45546) {
            *(int32_t *) &mem_47198.mem[res_43590 * 4] = res_43588;
        }
    }
    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
        return 1;
    
    bool res_43591;
    bool redout_45550 = 1;
    
    for (int32_t i_45551 = 0; i_45551 < 4; i_45551++) {
        int32_t x_43595 = *(int32_t *) &mem_47198.mem[i_45551 * 4];
        bool res_43596 = x_43595 == 2;
        bool x_43594 = res_43596 && redout_45550;
        bool redout_tmp_48243 = x_43594;
        
        redout_45550 = redout_tmp_48243;
    }
    res_43591 = redout_45550;
    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
        return 1;
    
    bool res_43597;
    
    if (res_43591) {
        struct memblock mem_47205;
        
        mem_47205.references = NULL;
        if (memblock_alloc(ctx, &mem_47205, 16, "mem_47205"))
            return 1;
        
        struct memblock mem_47208;
        
        mem_47208.references = NULL;
        if (memblock_alloc(ctx, &mem_47208, 16, "mem_47208"))
            return 1;
        
        int32_t discard_45564;
        int32_t scanacc_45558 = 0;
        
        for (int32_t i_45561 = 0; i_45561 < 4; i_45561++) {
            bool not_arg_43608 = i_45561 == 0;
            bool res_43609 = !not_arg_43608;
            int32_t part_res_43610;
            
            if (res_43609) {
                part_res_43610 = 0;
            } else {
                part_res_43610 = 1;
            }
            
            int32_t part_res_43611;
            
            if (res_43609) {
                part_res_43611 = 1;
            } else {
                part_res_43611 = 0;
            }
            
            int32_t zz_43606 = part_res_43611 + scanacc_45558;
            
            *(int32_t *) &mem_47205.mem[i_45561 * 4] = zz_43606;
            *(int32_t *) &mem_47208.mem[i_45561 * 4] = part_res_43610;
            
            int32_t scanacc_tmp_48244 = zz_43606;
            
            scanacc_45558 = scanacc_tmp_48244;
        }
        discard_45564 = scanacc_45558;
        
        int32_t last_offset_43612 = *(int32_t *) &mem_47205.mem[12];
        int64_t binop_x_47218 = sext_i32_i64(last_offset_43612);
        int64_t bytes_47217 = 4 * binop_x_47218;
        struct memblock mem_47219;
        
        mem_47219.references = NULL;
        if (memblock_alloc(ctx, &mem_47219, bytes_47217, "mem_47219"))
            return 1;
        for (int32_t write_iter_45565 = 0; write_iter_45565 < 4;
             write_iter_45565++) {
            int32_t write_iv_45567 =
                    *(int32_t *) &mem_47208.mem[write_iter_45565 * 4];
            int32_t write_iv_45568 =
                    *(int32_t *) &mem_47205.mem[write_iter_45565 * 4];
            bool is_this_one_43620 = write_iv_45567 == 0;
            int32_t this_offset_43621 = -1 + write_iv_45568;
            int32_t total_res_43622;
            
            if (is_this_one_43620) {
                total_res_43622 = this_offset_43621;
            } else {
                total_res_43622 = -1;
            }
            
            bool less_than_zzero_45572 = slt32(total_res_43622, 0);
            bool greater_than_sizze_45573 = sle32(last_offset_43612,
                                                  total_res_43622);
            bool outside_bounds_dim_45574 = less_than_zzero_45572 ||
                 greater_than_sizze_45573;
            
            if (!outside_bounds_dim_45574) {
                struct memblock mem_47224;
                
                mem_47224.references = NULL;
                if (memblock_alloc(ctx, &mem_47224, 4, "mem_47224"))
                    return 1;
                
                int32_t x_48249;
                
                for (int32_t i_48248 = 0; i_48248 < 1; i_48248++) {
                    x_48249 = write_iter_45565 + sext_i32_i32(i_48248);
                    *(int32_t *) &mem_47224.mem[i_48248 * 4] = x_48249;
                }
                memmove(mem_47219.mem + total_res_43622 * 4, mem_47224.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47224, "mem_47224") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47224, "mem_47224") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_47205, "mem_47205") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47208, "mem_47208") != 0)
            return 1;
        
        bool dim_eq_43624 = 3 == last_offset_43612;
        bool arrays_equal_43625;
        
        if (dim_eq_43624) {
            bool all_equal_43627;
            bool redout_45578 = 1;
            
            for (int32_t i_45579 = 0; i_45579 < 3; i_45579++) {
                int32_t x_43631 = *(int32_t *) &mem_47219.mem[i_45579 * 4];
                int32_t res_43633 = 2 * x_43631;
                int32_t res_43634 = 1 + i_45579;
                int32_t res_43635 = 2 * res_43634;
                bool res_43636 = res_43635 == res_43633;
                bool res_43630 = res_43636 && redout_45578;
                bool redout_tmp_48250 = res_43630;
                
                redout_45578 = redout_tmp_48250;
            }
            all_equal_43627 = redout_45578;
            arrays_equal_43625 = all_equal_43627;
        } else {
            arrays_equal_43625 = 0;
        }
        if (memblock_unref(ctx, &mem_47219, "mem_47219") != 0)
            return 1;
        res_43597 = arrays_equal_43625;
        if (memblock_unref(ctx, &mem_47219, "mem_47219") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47208, "mem_47208") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47205, "mem_47205") != 0)
            return 1;
    } else {
        res_43597 = 0;
    }
    
    struct memblock mem_47229;
    
    mem_47229.references = NULL;
    if (memblock_alloc(ctx, &mem_47229, 24, "mem_47229"))
        return 1;
    
    struct memblock mem_47234;
    
    mem_47234.references = NULL;
    if (memblock_alloc(ctx, &mem_47234, 8, "mem_47234"))
        return 1;
    for (int32_t i_45582 = 0; i_45582 < 3; i_45582++) {
        for (int32_t i_48252 = 0; i_48252 < 2; i_48252++) {
            *(int32_t *) &mem_47234.mem[i_48252 * 4] = i_45582;
        }
        memmove(mem_47229.mem + 2 * i_45582 * 4, mem_47234.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
        return 1;
    
    struct memblock mem_47239;
    
    mem_47239.references = NULL;
    if (memblock_alloc(ctx, &mem_47239, 24, "mem_47239"))
        return 1;
    
    struct memblock mem_47242;
    
    mem_47242.references = NULL;
    if (memblock_alloc(ctx, &mem_47242, 24, "mem_47242"))
        return 1;
    
    int32_t discard_45592;
    int32_t scanacc_45586 = 0;
    
    for (int32_t i_45589 = 0; i_45589 < 6; i_45589++) {
        bool not_arg_43651 = i_45589 == 0;
        bool res_43652 = !not_arg_43651;
        int32_t part_res_43653;
        
        if (res_43652) {
            part_res_43653 = 0;
        } else {
            part_res_43653 = 1;
        }
        
        int32_t part_res_43654;
        
        if (res_43652) {
            part_res_43654 = 1;
        } else {
            part_res_43654 = 0;
        }
        
        int32_t zz_43649 = part_res_43654 + scanacc_45586;
        
        *(int32_t *) &mem_47239.mem[i_45589 * 4] = zz_43649;
        *(int32_t *) &mem_47242.mem[i_45589 * 4] = part_res_43653;
        
        int32_t scanacc_tmp_48253 = zz_43649;
        
        scanacc_45586 = scanacc_tmp_48253;
    }
    discard_45592 = scanacc_45586;
    
    int32_t last_offset_43655 = *(int32_t *) &mem_47239.mem[20];
    int64_t binop_x_47252 = sext_i32_i64(last_offset_43655);
    int64_t bytes_47251 = 4 * binop_x_47252;
    struct memblock mem_47253;
    
    mem_47253.references = NULL;
    if (memblock_alloc(ctx, &mem_47253, bytes_47251, "mem_47253"))
        return 1;
    
    struct memblock mem_47256;
    
    mem_47256.references = NULL;
    if (memblock_alloc(ctx, &mem_47256, bytes_47251, "mem_47256"))
        return 1;
    
    struct memblock mem_47259;
    
    mem_47259.references = NULL;
    if (memblock_alloc(ctx, &mem_47259, bytes_47251, "mem_47259"))
        return 1;
    for (int32_t write_iter_45593 = 0; write_iter_45593 < 6;
         write_iter_45593++) {
        int32_t write_iv_45597 = *(int32_t *) &mem_47242.mem[write_iter_45593 *
                                                             4];
        int32_t write_iv_45598 = *(int32_t *) &mem_47239.mem[write_iter_45593 *
                                                             4];
        int32_t new_index_46364 = squot32(write_iter_45593, 2);
        int32_t binop_y_46366 = 2 * new_index_46364;
        int32_t new_index_46367 = write_iter_45593 - binop_y_46366;
        bool is_this_one_43667 = write_iv_45597 == 0;
        int32_t this_offset_43668 = -1 + write_iv_45598;
        int32_t total_res_43669;
        
        if (is_this_one_43667) {
            total_res_43669 = this_offset_43668;
        } else {
            total_res_43669 = -1;
        }
        
        bool less_than_zzero_45602 = slt32(total_res_43669, 0);
        bool greater_than_sizze_45603 = sle32(last_offset_43655,
                                              total_res_43669);
        bool outside_bounds_dim_45604 = less_than_zzero_45602 ||
             greater_than_sizze_45603;
        
        if (!outside_bounds_dim_45604) {
            memmove(mem_47253.mem + total_res_43669 * 4, mem_47229.mem + (2 *
                                                                          new_index_46364 +
                                                                          new_index_46367) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_45604) {
            struct memblock mem_47268;
            
            mem_47268.references = NULL;
            if (memblock_alloc(ctx, &mem_47268, 4, "mem_47268"))
                return 1;
            
            int32_t x_48260;
            
            for (int32_t i_48259 = 0; i_48259 < 1; i_48259++) {
                x_48260 = new_index_46367 + sext_i32_i32(i_48259);
                *(int32_t *) &mem_47268.mem[i_48259 * 4] = x_48260;
            }
            memmove(mem_47256.mem + total_res_43669 * 4, mem_47268.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_47268, "mem_47268") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47268, "mem_47268") != 0)
                return 1;
        }
        if (!outside_bounds_dim_45604) {
            struct memblock mem_47271;
            
            mem_47271.references = NULL;
            if (memblock_alloc(ctx, &mem_47271, 4, "mem_47271"))
                return 1;
            
            int32_t x_48262;
            
            for (int32_t i_48261 = 0; i_48261 < 1; i_48261++) {
                x_48262 = write_iter_45593 + sext_i32_i32(i_48261);
                *(int32_t *) &mem_47271.mem[i_48261 * 4] = x_48262;
            }
            memmove(mem_47259.mem + total_res_43669 * 4, mem_47271.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_47271, "mem_47271") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47271, "mem_47271") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
        return 1;
    
    bool empty_slice_43670 = last_offset_43655 == 0;
    int32_t m_43671 = last_offset_43655 - 1;
    bool zzero_leq_i_p_m_t_s_43672 = sle32(0, m_43671);
    struct memblock mem_47280;
    
    mem_47280.references = NULL;
    if (memblock_alloc(ctx, &mem_47280, bytes_47251, "mem_47280"))
        return 1;
    
    int32_t tmp_offs_48263 = 0;
    
    memmove(mem_47280.mem + tmp_offs_48263 * 4, mem_46817.mem + 0, 0);
    tmp_offs_48263 = tmp_offs_48263;
    memmove(mem_47280.mem + tmp_offs_48263 * 4, mem_47253.mem + 0,
            last_offset_43655 * sizeof(int32_t));
    tmp_offs_48263 += last_offset_43655;
    
    struct memblock mem_47283;
    
    mem_47283.references = NULL;
    if (memblock_alloc(ctx, &mem_47283, bytes_47251, "mem_47283"))
        return 1;
    
    int32_t tmp_offs_48264 = 0;
    
    memmove(mem_47283.mem + tmp_offs_48264 * 4, mem_46817.mem + 0, 0);
    tmp_offs_48264 = tmp_offs_48264;
    memmove(mem_47283.mem + tmp_offs_48264 * 4, mem_47256.mem + 0,
            last_offset_43655 * sizeof(int32_t));
    tmp_offs_48264 += last_offset_43655;
    
    struct memblock mem_47286;
    
    mem_47286.references = NULL;
    if (memblock_alloc(ctx, &mem_47286, bytes_47251, "mem_47286"))
        return 1;
    
    int32_t tmp_offs_48265 = 0;
    
    memmove(mem_47286.mem + tmp_offs_48265 * 4, mem_46817.mem + 0, 0);
    tmp_offs_48265 = tmp_offs_48265;
    memmove(mem_47286.mem + tmp_offs_48265 * 4, mem_47259.mem + 0,
            last_offset_43655 * sizeof(int32_t));
    tmp_offs_48265 += last_offset_43655;
    
    bool loop_cond_43682 = slt32(1, last_offset_43655);
    int32_t sizze_43683;
    int32_t sizze_43684;
    int32_t sizze_43685;
    int64_t res_mem_sizze_47329;
    struct memblock res_mem_47330;
    
    res_mem_47330.references = NULL;
    
    int64_t res_mem_sizze_47331;
    struct memblock res_mem_47332;
    
    res_mem_47332.references = NULL;
    
    int64_t res_mem_sizze_47333;
    struct memblock res_mem_47334;
    
    res_mem_47334.references = NULL;
    
    int32_t res_43689;
    
    if (empty_slice_43670) {
        struct memblock mem_47289;
        
        mem_47289.references = NULL;
        if (memblock_alloc(ctx, &mem_47289, bytes_47251, "mem_47289"))
            return 1;
        memmove(mem_47289.mem + 0, mem_47280.mem + 0, last_offset_43655 *
                sizeof(int32_t));
        
        struct memblock mem_47292;
        
        mem_47292.references = NULL;
        if (memblock_alloc(ctx, &mem_47292, bytes_47251, "mem_47292"))
            return 1;
        memmove(mem_47292.mem + 0, mem_47283.mem + 0, last_offset_43655 *
                sizeof(int32_t));
        
        struct memblock mem_47295;
        
        mem_47295.references = NULL;
        if (memblock_alloc(ctx, &mem_47295, bytes_47251, "mem_47295"))
            return 1;
        memmove(mem_47295.mem + 0, mem_47286.mem + 0, last_offset_43655 *
                sizeof(int32_t));
        sizze_43683 = last_offset_43655;
        sizze_43684 = last_offset_43655;
        sizze_43685 = last_offset_43655;
        res_mem_sizze_47329 = bytes_47251;
        if (memblock_set(ctx, &res_mem_47330, &mem_47289, "mem_47289") != 0)
            return 1;
        res_mem_sizze_47331 = bytes_47251;
        if (memblock_set(ctx, &res_mem_47332, &mem_47292, "mem_47292") != 0)
            return 1;
        res_mem_sizze_47333 = bytes_47251;
        if (memblock_set(ctx, &res_mem_47334, &mem_47295, "mem_47295") != 0)
            return 1;
        res_43689 = 0;
        if (memblock_unref(ctx, &mem_47295, "mem_47295") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47292, "mem_47292") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47289, "mem_47289") != 0)
            return 1;
    } else {
        bool res_43701;
        int32_t res_43702;
        int32_t res_43703;
        bool loop_while_43704;
        int32_t r_43705;
        int32_t n_43706;
        
        loop_while_43704 = loop_cond_43682;
        r_43705 = 0;
        n_43706 = last_offset_43655;
        while (loop_while_43704) {
            int32_t res_43707 = sdiv32(n_43706, 2);
            int32_t res_43708 = 1 + r_43705;
            bool loop_cond_43709 = slt32(1, res_43707);
            bool loop_while_tmp_48266 = loop_cond_43709;
            int32_t r_tmp_48267 = res_43708;
            int32_t n_tmp_48268;
            
            n_tmp_48268 = res_43707;
            loop_while_43704 = loop_while_tmp_48266;
            r_43705 = r_tmp_48267;
            n_43706 = n_tmp_48268;
        }
        res_43701 = loop_while_43704;
        res_43702 = r_43705;
        res_43703 = n_43706;
        
        int32_t y_43710 = 1 << res_43702;
        bool cond_43711 = last_offset_43655 == y_43710;
        int32_t y_43712 = 1 + res_43702;
        int32_t x_43713 = 1 << y_43712;
        int32_t arg_43714 = x_43713 - last_offset_43655;
        bool bounds_invalid_upwards_43715 = slt32(arg_43714, 0);
        int32_t conc_tmp_43716 = last_offset_43655 + arg_43714;
        int32_t sizze_43717;
        
        if (cond_43711) {
            sizze_43717 = last_offset_43655;
        } else {
            sizze_43717 = conc_tmp_43716;
        }
        
        int32_t res_43718;
        
        if (cond_43711) {
            res_43718 = res_43702;
        } else {
            res_43718 = y_43712;
        }
        
        int64_t binop_x_47315 = sext_i32_i64(conc_tmp_43716);
        int64_t bytes_47314 = 4 * binop_x_47315;
        int64_t res_mem_sizze_47323;
        struct memblock res_mem_47324;
        
        res_mem_47324.references = NULL;
        
        int64_t res_mem_sizze_47325;
        struct memblock res_mem_47326;
        
        res_mem_47326.references = NULL;
        
        int64_t res_mem_sizze_47327;
        struct memblock res_mem_47328;
        
        res_mem_47328.references = NULL;
        if (cond_43711) {
            struct memblock mem_47298;
            
            mem_47298.references = NULL;
            if (memblock_alloc(ctx, &mem_47298, bytes_47251, "mem_47298"))
                return 1;
            memmove(mem_47298.mem + 0, mem_47280.mem + 0, last_offset_43655 *
                    sizeof(int32_t));
            
            struct memblock mem_47301;
            
            mem_47301.references = NULL;
            if (memblock_alloc(ctx, &mem_47301, bytes_47251, "mem_47301"))
                return 1;
            memmove(mem_47301.mem + 0, mem_47283.mem + 0, last_offset_43655 *
                    sizeof(int32_t));
            
            struct memblock mem_47304;
            
            mem_47304.references = NULL;
            if (memblock_alloc(ctx, &mem_47304, bytes_47251, "mem_47304"))
                return 1;
            memmove(mem_47304.mem + 0, mem_47286.mem + 0, last_offset_43655 *
                    sizeof(int32_t));
            res_mem_sizze_47323 = bytes_47251;
            if (memblock_set(ctx, &res_mem_47324, &mem_47298, "mem_47298") != 0)
                return 1;
            res_mem_sizze_47325 = bytes_47251;
            if (memblock_set(ctx, &res_mem_47326, &mem_47301, "mem_47301") != 0)
                return 1;
            res_mem_sizze_47327 = bytes_47251;
            if (memblock_set(ctx, &res_mem_47328, &mem_47304, "mem_47304") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47304, "mem_47304") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47301, "mem_47301") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47298, "mem_47298") != 0)
                return 1;
        } else {
            bool y_43736 = slt32(0, last_offset_43655);
            bool index_certs_43737;
            
            if (!y_43736) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                       "Index [", 0,
                                       "] out of bounds for array of shape [",
                                       last_offset_43655, "].");
                if (memblock_unref(ctx, &res_mem_47328, "res_mem_47328") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47326, "res_mem_47326") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47324, "res_mem_47324") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                    return 1;
                return 1;
            }
            
            int32_t index_concat_43738 = *(int32_t *) &mem_47253.mem[0];
            int32_t index_concat_43739 = *(int32_t *) &mem_47256.mem[0];
            int32_t index_concat_43740 = *(int32_t *) &mem_47259.mem[0];
            int32_t res_43741;
            int32_t res_43742;
            int32_t res_43743;
            int32_t redout_45620;
            int32_t redout_45621;
            int32_t redout_45622;
            
            redout_45620 = index_concat_43738;
            redout_45621 = index_concat_43739;
            redout_45622 = index_concat_43740;
            for (int32_t i_45623 = 0; i_45623 < last_offset_43655; i_45623++) {
                bool index_concat_cmp_46394 = sle32(0, i_45623);
                int32_t index_concat_branch_46398;
                
                if (index_concat_cmp_46394) {
                    int32_t index_concat_46396 =
                            *(int32_t *) &mem_47253.mem[i_45623 * 4];
                    
                    index_concat_branch_46398 = index_concat_46396;
                } else {
                    int32_t index_concat_46397 =
                            *(int32_t *) &mem_46817.mem[i_45623 * 4];
                    
                    index_concat_branch_46398 = index_concat_46397;
                }
                
                int32_t index_concat_branch_46392;
                
                if (index_concat_cmp_46394) {
                    int32_t index_concat_46390 =
                            *(int32_t *) &mem_47256.mem[i_45623 * 4];
                    
                    index_concat_branch_46392 = index_concat_46390;
                } else {
                    int32_t index_concat_46391 =
                            *(int32_t *) &mem_46817.mem[i_45623 * 4];
                    
                    index_concat_branch_46392 = index_concat_46391;
                }
                
                int32_t index_concat_branch_46386;
                
                if (index_concat_cmp_46394) {
                    int32_t index_concat_46384 =
                            *(int32_t *) &mem_47259.mem[i_45623 * 4];
                    
                    index_concat_branch_46386 = index_concat_46384;
                } else {
                    int32_t index_concat_46385 =
                            *(int32_t *) &mem_46817.mem[i_45623 * 4];
                    
                    index_concat_branch_46386 = index_concat_46385;
                }
                
                bool cond_43750 = redout_45620 == index_concat_branch_46398;
                bool res_43751 = sle32(redout_45621, index_concat_branch_46392);
                bool res_43752 = sle32(redout_45620, index_concat_branch_46398);
                bool x_43753 = cond_43750 && res_43751;
                bool x_43754 = !cond_43750;
                bool y_43755 = res_43752 && x_43754;
                bool res_43756 = x_43753 || y_43755;
                int32_t res_43757;
                
                if (res_43756) {
                    res_43757 = index_concat_branch_46398;
                } else {
                    res_43757 = redout_45620;
                }
                
                int32_t res_43758;
                
                if (res_43756) {
                    res_43758 = index_concat_branch_46392;
                } else {
                    res_43758 = redout_45621;
                }
                
                int32_t res_43759;
                
                if (res_43756) {
                    res_43759 = index_concat_branch_46386;
                } else {
                    res_43759 = redout_45622;
                }
                
                int32_t redout_tmp_48269 = res_43757;
                int32_t redout_tmp_48270 = res_43758;
                int32_t redout_tmp_48271;
                
                redout_tmp_48271 = res_43759;
                redout_45620 = redout_tmp_48269;
                redout_45621 = redout_tmp_48270;
                redout_45622 = redout_tmp_48271;
            }
            res_43741 = redout_45620;
            res_43742 = redout_45621;
            res_43743 = redout_45622;
            
            bool eq_x_zz_43763 = 0 == arg_43714;
            bool not_p_43764 = !bounds_invalid_upwards_43715;
            bool p_and_eq_x_y_43765 = eq_x_zz_43763 && not_p_43764;
            bool dim_zzero_43766 = bounds_invalid_upwards_43715 ||
                 p_and_eq_x_y_43765;
            bool both_empty_43767 = eq_x_zz_43763 && dim_zzero_43766;
            bool eq_x_y_43768 = arg_43714 == 0;
            bool p_and_eq_x_y_43769 = bounds_invalid_upwards_43715 &&
                 eq_x_y_43768;
            bool dim_match_43770 = not_p_43764 || p_and_eq_x_y_43769;
            bool empty_or_match_43771 = both_empty_43767 || dim_match_43770;
            bool empty_or_match_cert_43772;
            
            if (!empty_or_match_43771) {
                ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                       "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                       "Function return value does not match shape of type ",
                                       "*", "[", arg_43714, "]", "t");
                if (memblock_unref(ctx, &res_mem_47328, "res_mem_47328") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47326, "res_mem_47326") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47324, "res_mem_47324") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                    return 1;
                return 1;
            }
            
            int64_t binop_x_47306 = sext_i32_i64(arg_43714);
            int64_t bytes_47305 = 4 * binop_x_47306;
            struct memblock mem_47307;
            
            mem_47307.references = NULL;
            if (memblock_alloc(ctx, &mem_47307, bytes_47305, "mem_47307"))
                return 1;
            for (int32_t i_48272 = 0; i_48272 < arg_43714; i_48272++) {
                *(int32_t *) &mem_47307.mem[i_48272 * 4] = res_43741;
            }
            
            struct memblock mem_47310;
            
            mem_47310.references = NULL;
            if (memblock_alloc(ctx, &mem_47310, bytes_47305, "mem_47310"))
                return 1;
            for (int32_t i_48273 = 0; i_48273 < arg_43714; i_48273++) {
                *(int32_t *) &mem_47310.mem[i_48273 * 4] = res_43742;
            }
            
            struct memblock mem_47313;
            
            mem_47313.references = NULL;
            if (memblock_alloc(ctx, &mem_47313, bytes_47305, "mem_47313"))
                return 1;
            for (int32_t i_48274 = 0; i_48274 < arg_43714; i_48274++) {
                *(int32_t *) &mem_47313.mem[i_48274 * 4] = res_43743;
            }
            
            struct memblock mem_47316;
            
            mem_47316.references = NULL;
            if (memblock_alloc(ctx, &mem_47316, bytes_47314, "mem_47316"))
                return 1;
            
            int32_t tmp_offs_48275 = 0;
            
            memmove(mem_47316.mem + tmp_offs_48275 * 4, mem_46817.mem + 0, 0);
            tmp_offs_48275 = tmp_offs_48275;
            memmove(mem_47316.mem + tmp_offs_48275 * 4, mem_47253.mem + 0,
                    last_offset_43655 * sizeof(int32_t));
            tmp_offs_48275 += last_offset_43655;
            memmove(mem_47316.mem + tmp_offs_48275 * 4, mem_47307.mem + 0,
                    arg_43714 * sizeof(int32_t));
            tmp_offs_48275 += arg_43714;
            if (memblock_unref(ctx, &mem_47307, "mem_47307") != 0)
                return 1;
            
            struct memblock mem_47319;
            
            mem_47319.references = NULL;
            if (memblock_alloc(ctx, &mem_47319, bytes_47314, "mem_47319"))
                return 1;
            
            int32_t tmp_offs_48276 = 0;
            
            memmove(mem_47319.mem + tmp_offs_48276 * 4, mem_46817.mem + 0, 0);
            tmp_offs_48276 = tmp_offs_48276;
            memmove(mem_47319.mem + tmp_offs_48276 * 4, mem_47256.mem + 0,
                    last_offset_43655 * sizeof(int32_t));
            tmp_offs_48276 += last_offset_43655;
            memmove(mem_47319.mem + tmp_offs_48276 * 4, mem_47310.mem + 0,
                    arg_43714 * sizeof(int32_t));
            tmp_offs_48276 += arg_43714;
            if (memblock_unref(ctx, &mem_47310, "mem_47310") != 0)
                return 1;
            
            struct memblock mem_47322;
            
            mem_47322.references = NULL;
            if (memblock_alloc(ctx, &mem_47322, bytes_47314, "mem_47322"))
                return 1;
            
            int32_t tmp_offs_48277 = 0;
            
            memmove(mem_47322.mem + tmp_offs_48277 * 4, mem_46817.mem + 0, 0);
            tmp_offs_48277 = tmp_offs_48277;
            memmove(mem_47322.mem + tmp_offs_48277 * 4, mem_47259.mem + 0,
                    last_offset_43655 * sizeof(int32_t));
            tmp_offs_48277 += last_offset_43655;
            memmove(mem_47322.mem + tmp_offs_48277 * 4, mem_47313.mem + 0,
                    arg_43714 * sizeof(int32_t));
            tmp_offs_48277 += arg_43714;
            if (memblock_unref(ctx, &mem_47313, "mem_47313") != 0)
                return 1;
            res_mem_sizze_47323 = bytes_47314;
            if (memblock_set(ctx, &res_mem_47324, &mem_47316, "mem_47316") != 0)
                return 1;
            res_mem_sizze_47325 = bytes_47314;
            if (memblock_set(ctx, &res_mem_47326, &mem_47319, "mem_47319") != 0)
                return 1;
            res_mem_sizze_47327 = bytes_47314;
            if (memblock_set(ctx, &res_mem_47328, &mem_47322, "mem_47322") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47322, "mem_47322") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47319, "mem_47319") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47316, "mem_47316") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47313, "mem_47313") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47310, "mem_47310") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47307, "mem_47307") != 0)
                return 1;
        }
        sizze_43683 = sizze_43717;
        sizze_43684 = sizze_43717;
        sizze_43685 = sizze_43717;
        res_mem_sizze_47329 = res_mem_sizze_47323;
        if (memblock_set(ctx, &res_mem_47330, &res_mem_47324,
                         "res_mem_47324") != 0)
            return 1;
        res_mem_sizze_47331 = res_mem_sizze_47325;
        if (memblock_set(ctx, &res_mem_47332, &res_mem_47326,
                         "res_mem_47326") != 0)
            return 1;
        res_mem_sizze_47333 = res_mem_sizze_47327;
        if (memblock_set(ctx, &res_mem_47334, &res_mem_47328,
                         "res_mem_47328") != 0)
            return 1;
        res_43689 = res_43718;
        if (memblock_unref(ctx, &res_mem_47328, "res_mem_47328") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47326, "res_mem_47326") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47324, "res_mem_47324") != 0)
            return 1;
    }
    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
        return 1;
    
    bool dim_zzero_43782 = 0 == sizze_43684;
    bool dim_zzero_43783 = 0 == sizze_43683;
    bool both_empty_43784 = dim_zzero_43782 && dim_zzero_43783;
    bool dim_match_43785 = sizze_43683 == sizze_43684;
    bool empty_or_match_43786 = both_empty_43784 || dim_match_43785;
    bool empty_or_match_cert_43787;
    
    if (!empty_or_match_43786) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
            return 1;
        return 1;
    }
    
    bool dim_zzero_43789 = 0 == sizze_43685;
    bool both_empty_43790 = dim_zzero_43783 && dim_zzero_43789;
    bool dim_match_43791 = sizze_43683 == sizze_43685;
    bool empty_or_match_43792 = both_empty_43790 || dim_match_43791;
    bool empty_or_match_cert_43793;
    
    if (!empty_or_match_43792) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
            return 1;
        return 1;
    }
    
    int64_t binop_x_47348 = sext_i32_i64(sizze_43683);
    int64_t bytes_47347 = 4 * binop_x_47348;
    int64_t indexed_mem_sizze_47374;
    struct memblock indexed_mem_47375;
    
    indexed_mem_47375.references = NULL;
    
    int64_t indexed_mem_sizze_47376;
    struct memblock indexed_mem_47377;
    
    indexed_mem_47377.references = NULL;
    
    int64_t indexed_mem_sizze_47378;
    struct memblock indexed_mem_47379;
    
    indexed_mem_47379.references = NULL;
    
    int64_t xs_mem_sizze_47335;
    struct memblock xs_mem_47336;
    
    xs_mem_47336.references = NULL;
    
    int64_t xs_mem_sizze_47337;
    struct memblock xs_mem_47338;
    
    xs_mem_47338.references = NULL;
    
    int64_t xs_mem_sizze_47339;
    struct memblock xs_mem_47340;
    
    xs_mem_47340.references = NULL;
    xs_mem_sizze_47335 = res_mem_sizze_47329;
    if (memblock_set(ctx, &xs_mem_47336, &res_mem_47330, "res_mem_47330") != 0)
        return 1;
    xs_mem_sizze_47337 = res_mem_sizze_47331;
    if (memblock_set(ctx, &xs_mem_47338, &res_mem_47332, "res_mem_47332") != 0)
        return 1;
    xs_mem_sizze_47339 = res_mem_sizze_47333;
    if (memblock_set(ctx, &xs_mem_47340, &res_mem_47334, "res_mem_47334") != 0)
        return 1;
    for (int32_t i_43810 = 0; i_43810 < res_43689; i_43810++) {
        int32_t upper_bound_43811 = 1 + i_43810;
        int64_t res_mem_sizze_47368;
        struct memblock res_mem_47369;
        
        res_mem_47369.references = NULL;
        
        int64_t res_mem_sizze_47370;
        struct memblock res_mem_47371;
        
        res_mem_47371.references = NULL;
        
        int64_t res_mem_sizze_47372;
        struct memblock res_mem_47373;
        
        res_mem_47373.references = NULL;
        
        int64_t xs_mem_sizze_47341;
        struct memblock xs_mem_47342;
        
        xs_mem_47342.references = NULL;
        
        int64_t xs_mem_sizze_47343;
        struct memblock xs_mem_47344;
        
        xs_mem_47344.references = NULL;
        
        int64_t xs_mem_sizze_47345;
        struct memblock xs_mem_47346;
        
        xs_mem_47346.references = NULL;
        xs_mem_sizze_47341 = xs_mem_sizze_47335;
        if (memblock_set(ctx, &xs_mem_47342, &xs_mem_47336, "xs_mem_47336") !=
            0)
            return 1;
        xs_mem_sizze_47343 = xs_mem_sizze_47337;
        if (memblock_set(ctx, &xs_mem_47344, &xs_mem_47338, "xs_mem_47338") !=
            0)
            return 1;
        xs_mem_sizze_47345 = xs_mem_sizze_47339;
        if (memblock_set(ctx, &xs_mem_47346, &xs_mem_47340, "xs_mem_47340") !=
            0)
            return 1;
        for (int32_t j_43822 = 0; j_43822 < upper_bound_43811; j_43822++) {
            int32_t y_43823 = i_43810 - j_43822;
            int32_t res_43824 = 1 << y_43823;
            struct memblock mem_47349;
            
            mem_47349.references = NULL;
            if (memblock_alloc(ctx, &mem_47349, bytes_47347, "mem_47349"))
                return 1;
            
            struct memblock mem_47352;
            
            mem_47352.references = NULL;
            if (memblock_alloc(ctx, &mem_47352, bytes_47347, "mem_47352"))
                return 1;
            
            struct memblock mem_47355;
            
            mem_47355.references = NULL;
            if (memblock_alloc(ctx, &mem_47355, bytes_47347, "mem_47355"))
                return 1;
            for (int32_t i_45630 = 0; i_45630 < sizze_43683; i_45630++) {
                int32_t res_43829 = *(int32_t *) &xs_mem_47342.mem[i_45630 * 4];
                int32_t res_43830 = *(int32_t *) &xs_mem_47344.mem[i_45630 * 4];
                int32_t res_43831 = *(int32_t *) &xs_mem_47346.mem[i_45630 * 4];
                int32_t x_43832 = ashr32(i_45630, i_43810);
                int32_t x_43833 = 2 & x_43832;
                bool res_43834 = x_43833 == 0;
                int32_t x_43835 = res_43824 & i_45630;
                bool cond_43836 = x_43835 == 0;
                int32_t res_43837;
                int32_t res_43838;
                int32_t res_43839;
                
                if (cond_43836) {
                    int32_t i_43840 = res_43824 | i_45630;
                    int32_t res_43841 = *(int32_t *) &xs_mem_47342.mem[i_43840 *
                                                                       4];
                    int32_t res_43842 = *(int32_t *) &xs_mem_47344.mem[i_43840 *
                                                                       4];
                    int32_t res_43843 = *(int32_t *) &xs_mem_47346.mem[i_43840 *
                                                                       4];
                    bool cond_43844 = res_43841 == res_43829;
                    bool res_43845 = sle32(res_43842, res_43830);
                    bool res_43846 = sle32(res_43841, res_43829);
                    bool x_43847 = cond_43844 && res_43845;
                    bool x_43848 = !cond_43844;
                    bool y_43849 = res_43846 && x_43848;
                    bool res_43850 = x_43847 || y_43849;
                    bool cond_43851 = res_43850 == res_43834;
                    int32_t res_43852;
                    
                    if (cond_43851) {
                        res_43852 = res_43841;
                    } else {
                        res_43852 = res_43829;
                    }
                    
                    int32_t res_43853;
                    
                    if (cond_43851) {
                        res_43853 = res_43842;
                    } else {
                        res_43853 = res_43830;
                    }
                    
                    int32_t res_43854;
                    
                    if (cond_43851) {
                        res_43854 = res_43843;
                    } else {
                        res_43854 = res_43831;
                    }
                    res_43837 = res_43852;
                    res_43838 = res_43853;
                    res_43839 = res_43854;
                } else {
                    int32_t i_43855 = res_43824 ^ i_45630;
                    int32_t res_43856 = *(int32_t *) &xs_mem_47342.mem[i_43855 *
                                                                       4];
                    int32_t res_43857 = *(int32_t *) &xs_mem_47344.mem[i_43855 *
                                                                       4];
                    int32_t res_43858 = *(int32_t *) &xs_mem_47346.mem[i_43855 *
                                                                       4];
                    bool cond_43859 = res_43829 == res_43856;
                    bool res_43860 = sle32(res_43830, res_43857);
                    bool res_43861 = sle32(res_43829, res_43856);
                    bool x_43862 = cond_43859 && res_43860;
                    bool x_43863 = !cond_43859;
                    bool y_43864 = res_43861 && x_43863;
                    bool res_43865 = x_43862 || y_43864;
                    bool cond_43866 = res_43865 == res_43834;
                    int32_t res_43867;
                    
                    if (cond_43866) {
                        res_43867 = res_43856;
                    } else {
                        res_43867 = res_43829;
                    }
                    
                    int32_t res_43868;
                    
                    if (cond_43866) {
                        res_43868 = res_43857;
                    } else {
                        res_43868 = res_43830;
                    }
                    
                    int32_t res_43869;
                    
                    if (cond_43866) {
                        res_43869 = res_43858;
                    } else {
                        res_43869 = res_43831;
                    }
                    res_43837 = res_43867;
                    res_43838 = res_43868;
                    res_43839 = res_43869;
                }
                *(int32_t *) &mem_47349.mem[i_45630 * 4] = res_43837;
                *(int32_t *) &mem_47352.mem[i_45630 * 4] = res_43838;
                *(int32_t *) &mem_47355.mem[i_45630 * 4] = res_43839;
            }
            
            int64_t xs_mem_sizze_tmp_48287 = bytes_47347;
            struct memblock xs_mem_tmp_48288;
            
            xs_mem_tmp_48288.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48288, &mem_47349, "mem_47349") !=
                0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48289 = bytes_47347;
            struct memblock xs_mem_tmp_48290;
            
            xs_mem_tmp_48290.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48290, &mem_47352, "mem_47352") !=
                0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48291 = bytes_47347;
            struct memblock xs_mem_tmp_48292;
            
            xs_mem_tmp_48292.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48292, &mem_47355, "mem_47355") !=
                0)
                return 1;
            xs_mem_sizze_47341 = xs_mem_sizze_tmp_48287;
            if (memblock_set(ctx, &xs_mem_47342, &xs_mem_tmp_48288,
                             "xs_mem_tmp_48288") != 0)
                return 1;
            xs_mem_sizze_47343 = xs_mem_sizze_tmp_48289;
            if (memblock_set(ctx, &xs_mem_47344, &xs_mem_tmp_48290,
                             "xs_mem_tmp_48290") != 0)
                return 1;
            xs_mem_sizze_47345 = xs_mem_sizze_tmp_48291;
            if (memblock_set(ctx, &xs_mem_47346, &xs_mem_tmp_48292,
                             "xs_mem_tmp_48292") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48292, "xs_mem_tmp_48292") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48290, "xs_mem_tmp_48290") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48288, "xs_mem_tmp_48288") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47355, "mem_47355") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47352, "mem_47352") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47349, "mem_47349") != 0)
                return 1;
        }
        res_mem_sizze_47368 = xs_mem_sizze_47341;
        if (memblock_set(ctx, &res_mem_47369, &xs_mem_47342, "xs_mem_47342") !=
            0)
            return 1;
        res_mem_sizze_47370 = xs_mem_sizze_47343;
        if (memblock_set(ctx, &res_mem_47371, &xs_mem_47344, "xs_mem_47344") !=
            0)
            return 1;
        res_mem_sizze_47372 = xs_mem_sizze_47345;
        if (memblock_set(ctx, &res_mem_47373, &xs_mem_47346, "xs_mem_47346") !=
            0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_48278 = res_mem_sizze_47368;
        struct memblock xs_mem_tmp_48279;
        
        xs_mem_tmp_48279.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_48279, &res_mem_47369,
                         "res_mem_47369") != 0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_48280 = res_mem_sizze_47370;
        struct memblock xs_mem_tmp_48281;
        
        xs_mem_tmp_48281.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_48281, &res_mem_47371,
                         "res_mem_47371") != 0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_48282 = res_mem_sizze_47372;
        struct memblock xs_mem_tmp_48283;
        
        xs_mem_tmp_48283.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_48283, &res_mem_47373,
                         "res_mem_47373") != 0)
            return 1;
        xs_mem_sizze_47335 = xs_mem_sizze_tmp_48278;
        if (memblock_set(ctx, &xs_mem_47336, &xs_mem_tmp_48279,
                         "xs_mem_tmp_48279") != 0)
            return 1;
        xs_mem_sizze_47337 = xs_mem_sizze_tmp_48280;
        if (memblock_set(ctx, &xs_mem_47338, &xs_mem_tmp_48281,
                         "xs_mem_tmp_48281") != 0)
            return 1;
        xs_mem_sizze_47339 = xs_mem_sizze_tmp_48282;
        if (memblock_set(ctx, &xs_mem_47340, &xs_mem_tmp_48283,
                         "xs_mem_tmp_48283") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_48283, "xs_mem_tmp_48283") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_48281, "xs_mem_tmp_48281") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_48279, "xs_mem_tmp_48279") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47346, "xs_mem_47346") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47344, "xs_mem_47344") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47342, "xs_mem_47342") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47373, "res_mem_47373") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47371, "res_mem_47371") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47369, "res_mem_47369") != 0)
            return 1;
    }
    indexed_mem_sizze_47374 = xs_mem_sizze_47335;
    if (memblock_set(ctx, &indexed_mem_47375, &xs_mem_47336, "xs_mem_47336") !=
        0)
        return 1;
    indexed_mem_sizze_47376 = xs_mem_sizze_47337;
    if (memblock_set(ctx, &indexed_mem_47377, &xs_mem_47338, "xs_mem_47338") !=
        0)
        return 1;
    indexed_mem_sizze_47378 = xs_mem_sizze_47339;
    if (memblock_set(ctx, &indexed_mem_47379, &xs_mem_47340, "xs_mem_47340") !=
        0)
        return 1;
    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
        return 1;
    
    bool i_p_m_t_s_leq_w_43870 = slt32(m_43671, sizze_43683);
    bool y_43871 = zzero_leq_i_p_m_t_s_43672 && i_p_m_t_s_leq_w_43870;
    bool ok_or_empty_43873 = empty_slice_43670 || y_43871;
    bool index_certs_43874;
    
    if (!ok_or_empty_43873) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                               "Index [", "", ":", last_offset_43655,
                               "] out of bounds for array of shape [",
                               sizze_43683, "].");
        if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
            return 1;
        return 1;
    }
    
    struct memblock mem_47382;
    
    mem_47382.references = NULL;
    if (memblock_alloc(ctx, &mem_47382, bytes_47251, "mem_47382"))
        return 1;
    
    struct memblock mem_47384;
    
    mem_47384.references = NULL;
    if (memblock_alloc(ctx, &mem_47384, binop_x_47252, "mem_47384"))
        return 1;
    
    int32_t discard_45657;
    int32_t scanacc_45642 = 0;
    
    for (int32_t i_45648 = 0; i_45648 < last_offset_43655; i_45648++) {
        int32_t x_43896 = *(int32_t *) &indexed_mem_47375.mem[i_45648 * 4];
        int32_t x_43897 = *(int32_t *) &indexed_mem_47377.mem[i_45648 * 4];
        int32_t i_p_o_46412 = -1 + i_45648;
        int32_t rot_i_46413 = smod32(i_p_o_46412, last_offset_43655);
        int32_t x_43898 = *(int32_t *) &indexed_mem_47375.mem[rot_i_46413 * 4];
        int32_t x_43899 = *(int32_t *) &indexed_mem_47377.mem[rot_i_46413 * 4];
        int32_t x_43900 = *(int32_t *) &indexed_mem_47379.mem[i_45648 * 4];
        bool res_43901 = x_43896 == x_43898;
        bool res_43902 = x_43897 == x_43899;
        bool eq_43903 = res_43901 && res_43902;
        bool res_43904 = !eq_43903;
        int32_t res_43894;
        
        if (res_43904) {
            res_43894 = x_43900;
        } else {
            int32_t res_43895 = x_43900 + scanacc_45642;
            
            res_43894 = res_43895;
        }
        *(int32_t *) &mem_47382.mem[i_45648 * 4] = res_43894;
        *(bool *) &mem_47384.mem[i_45648] = res_43904;
        
        int32_t scanacc_tmp_48299 = res_43894;
        
        scanacc_45642 = scanacc_tmp_48299;
    }
    discard_45657 = scanacc_45642;
    if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") != 0)
        return 1;
    
    struct memblock mem_47395;
    
    mem_47395.references = NULL;
    if (memblock_alloc(ctx, &mem_47395, bytes_47251, "mem_47395"))
        return 1;
    
    int32_t discard_45663;
    int32_t scanacc_45659 = 0;
    
    for (int32_t i_45661 = 0; i_45661 < last_offset_43655; i_45661++) {
        int32_t i_p_o_46420 = 1 + i_45661;
        int32_t rot_i_46421 = smod32(i_p_o_46420, last_offset_43655);
        bool x_43910 = *(bool *) &mem_47384.mem[rot_i_46421];
        int32_t res_43911 = btoi_bool_i32(x_43910);
        int32_t res_43909 = res_43911 + scanacc_45659;
        
        *(int32_t *) &mem_47395.mem[i_45661 * 4] = res_43909;
        
        int32_t scanacc_tmp_48302 = res_43909;
        
        scanacc_45659 = scanacc_tmp_48302;
    }
    discard_45663 = scanacc_45659;
    
    int32_t res_43912;
    
    if (loop_cond_43682) {
        bool index_certs_43913;
        
        if (!zzero_leq_i_p_m_t_s_43672) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                   "Index [", m_43671,
                                   "] out of bounds for array of shape [",
                                   last_offset_43655, "].");
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int32_t res_43914 = *(int32_t *) &mem_47395.mem[m_43671 * 4];
        
        res_43912 = res_43914;
    } else {
        res_43912 = 0;
    }
    
    bool bounds_invalid_upwards_43915 = slt32(res_43912, 0);
    bool eq_x_zz_43916 = 0 == res_43912;
    bool not_p_43917 = !bounds_invalid_upwards_43915;
    bool p_and_eq_x_y_43918 = eq_x_zz_43916 && not_p_43917;
    bool dim_zzero_43919 = bounds_invalid_upwards_43915 || p_and_eq_x_y_43918;
    bool both_empty_43920 = eq_x_zz_43916 && dim_zzero_43919;
    bool eq_x_y_43921 = res_43912 == 0;
    bool empty_or_match_43924 = not_p_43917 || both_empty_43920;
    bool empty_or_match_cert_43925;
    
    if (!empty_or_match_43924) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                               "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                               "Function return value does not match shape of type ",
                               "*", "[", res_43912, "]", "t");
        if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
            return 1;
        return 1;
    }
    
    int64_t binop_x_47401 = sext_i32_i64(res_43912);
    int64_t bytes_47400 = 4 * binop_x_47401;
    struct memblock mem_47402;
    
    mem_47402.references = NULL;
    if (memblock_alloc(ctx, &mem_47402, bytes_47400, "mem_47402"))
        return 1;
    for (int32_t i_48304 = 0; i_48304 < res_43912; i_48304++) {
        *(int32_t *) &mem_47402.mem[i_48304 * 4] = 0;
    }
    for (int32_t write_iter_45664 = 0; write_iter_45664 < last_offset_43655;
         write_iter_45664++) {
        int32_t write_iv_45668 = *(int32_t *) &mem_47395.mem[write_iter_45664 *
                                                             4];
        int32_t i_p_o_46425 = 1 + write_iter_45664;
        int32_t rot_i_46426 = smod32(i_p_o_46425, last_offset_43655);
        bool write_iv_45669 = *(bool *) &mem_47384.mem[rot_i_46426];
        int32_t res_43937;
        
        if (write_iv_45669) {
            int32_t res_43938 = write_iv_45668 - 1;
            
            res_43937 = res_43938;
        } else {
            res_43937 = -1;
        }
        
        bool less_than_zzero_45685 = slt32(res_43937, 0);
        bool greater_than_sizze_45686 = sle32(res_43912, res_43937);
        bool outside_bounds_dim_45687 = less_than_zzero_45685 ||
             greater_than_sizze_45686;
        
        if (!outside_bounds_dim_45687) {
            memmove(mem_47402.mem + res_43937 * 4, mem_47382.mem +
                    write_iter_45664 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
        return 1;
    
    struct memblock mem_47409;
    
    mem_47409.references = NULL;
    if (memblock_alloc(ctx, &mem_47409, bytes_47400, "mem_47409"))
        return 1;
    
    struct memblock mem_47412;
    
    mem_47412.references = NULL;
    if (memblock_alloc(ctx, &mem_47412, bytes_47400, "mem_47412"))
        return 1;
    
    int32_t discard_45699;
    int32_t scanacc_45693 = 0;
    
    for (int32_t i_45696 = 0; i_45696 < res_43912; i_45696++) {
        int32_t x_43944 = *(int32_t *) &mem_47402.mem[i_45696 * 4];
        bool not_arg_43945 = x_43944 == 0;
        bool res_43946 = !not_arg_43945;
        int32_t part_res_43947;
        
        if (res_43946) {
            part_res_43947 = 0;
        } else {
            part_res_43947 = 1;
        }
        
        int32_t part_res_43948;
        
        if (res_43946) {
            part_res_43948 = 1;
        } else {
            part_res_43948 = 0;
        }
        
        int32_t zz_43943 = part_res_43948 + scanacc_45693;
        
        *(int32_t *) &mem_47409.mem[i_45696 * 4] = zz_43943;
        *(int32_t *) &mem_47412.mem[i_45696 * 4] = part_res_43947;
        
        int32_t scanacc_tmp_48306 = zz_43943;
        
        scanacc_45693 = scanacc_tmp_48306;
    }
    discard_45699 = scanacc_45693;
    
    int32_t last_index_43949 = res_43912 - 1;
    int32_t partition_sizze_43950;
    
    if (eq_x_y_43921) {
        partition_sizze_43950 = 0;
    } else {
        int32_t last_offset_43951 =
                *(int32_t *) &mem_47409.mem[last_index_43949 * 4];
        
        partition_sizze_43950 = last_offset_43951;
    }
    
    int64_t binop_x_47422 = sext_i32_i64(partition_sizze_43950);
    int64_t bytes_47421 = 4 * binop_x_47422;
    struct memblock mem_47423;
    
    mem_47423.references = NULL;
    if (memblock_alloc(ctx, &mem_47423, bytes_47421, "mem_47423"))
        return 1;
    for (int32_t write_iter_45700 = 0; write_iter_45700 < res_43912;
         write_iter_45700++) {
        int32_t write_iv_45702 = *(int32_t *) &mem_47412.mem[write_iter_45700 *
                                                             4];
        int32_t write_iv_45703 = *(int32_t *) &mem_47409.mem[write_iter_45700 *
                                                             4];
        bool is_this_one_43959 = write_iv_45702 == 0;
        int32_t this_offset_43960 = -1 + write_iv_45703;
        int32_t total_res_43961;
        
        if (is_this_one_43959) {
            total_res_43961 = this_offset_43960;
        } else {
            total_res_43961 = -1;
        }
        
        bool less_than_zzero_45707 = slt32(total_res_43961, 0);
        bool greater_than_sizze_45708 = sle32(partition_sizze_43950,
                                              total_res_43961);
        bool outside_bounds_dim_45709 = less_than_zzero_45707 ||
             greater_than_sizze_45708;
        
        if (!outside_bounds_dim_45709) {
            memmove(mem_47423.mem + total_res_43961 * 4, mem_47402.mem +
                    write_iter_45700 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
        return 1;
    
    bool cond_43962 = partition_sizze_43950 == 5;
    bool res_43963;
    
    if (cond_43962) {
        bool arrays_equal_43964;
        
        if (cond_43962) {
            bool all_equal_43966;
            bool redout_45713 = 1;
            
            for (int32_t i_45714 = 0; i_45714 < partition_sizze_43950;
                 i_45714++) {
                int32_t x_43971 = *(int32_t *) &mem_47423.mem[i_45714 * 4];
                int32_t res_43972 = 1 + i_45714;
                bool res_43973 = x_43971 == res_43972;
                bool res_43969 = res_43973 && redout_45713;
                bool redout_tmp_48310 = res_43969;
                
                redout_45713 = redout_tmp_48310;
            }
            all_equal_43966 = redout_45713;
            arrays_equal_43964 = all_equal_43966;
        } else {
            arrays_equal_43964 = 0;
        }
        res_43963 = arrays_equal_43964;
    } else {
        res_43963 = 0;
    }
    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
        return 1;
    
    bool cond_43974;
    
    if (res_43963) {
        struct memblock mem_47430;
        
        mem_47430.references = NULL;
        if (memblock_alloc(ctx, &mem_47430, 24, "mem_47430"))
            return 1;
        
        struct memblock mem_47435;
        
        mem_47435.references = NULL;
        if (memblock_alloc(ctx, &mem_47435, 8, "mem_47435"))
            return 1;
        for (int32_t i_45717 = 0; i_45717 < 3; i_45717++) {
            for (int32_t i_48312 = 0; i_48312 < 2; i_48312++) {
                *(int32_t *) &mem_47435.mem[i_48312 * 4] = i_45717;
            }
            memmove(mem_47430.mem + 2 * i_45717 * 4, mem_47435.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
            return 1;
        
        struct memblock mem_47440;
        
        mem_47440.references = NULL;
        if (memblock_alloc(ctx, &mem_47440, 24, "mem_47440"))
            return 1;
        
        struct memblock mem_47443;
        
        mem_47443.references = NULL;
        if (memblock_alloc(ctx, &mem_47443, 24, "mem_47443"))
            return 1;
        
        int32_t discard_45727;
        int32_t scanacc_45721 = 0;
        
        for (int32_t i_45724 = 0; i_45724 < 6; i_45724++) {
            bool not_arg_43985 = i_45724 == 0;
            bool res_43986 = !not_arg_43985;
            int32_t part_res_43987;
            
            if (res_43986) {
                part_res_43987 = 0;
            } else {
                part_res_43987 = 1;
            }
            
            int32_t part_res_43988;
            
            if (res_43986) {
                part_res_43988 = 1;
            } else {
                part_res_43988 = 0;
            }
            
            int32_t zz_43983 = part_res_43988 + scanacc_45721;
            
            *(int32_t *) &mem_47440.mem[i_45724 * 4] = zz_43983;
            *(int32_t *) &mem_47443.mem[i_45724 * 4] = part_res_43987;
            
            int32_t scanacc_tmp_48313 = zz_43983;
            
            scanacc_45721 = scanacc_tmp_48313;
        }
        discard_45727 = scanacc_45721;
        
        int32_t last_offset_43989 = *(int32_t *) &mem_47440.mem[20];
        int64_t binop_x_47453 = sext_i32_i64(last_offset_43989);
        int64_t bytes_47452 = 4 * binop_x_47453;
        struct memblock mem_47454;
        
        mem_47454.references = NULL;
        if (memblock_alloc(ctx, &mem_47454, bytes_47452, "mem_47454"))
            return 1;
        
        struct memblock mem_47457;
        
        mem_47457.references = NULL;
        if (memblock_alloc(ctx, &mem_47457, bytes_47452, "mem_47457"))
            return 1;
        
        struct memblock mem_47460;
        
        mem_47460.references = NULL;
        if (memblock_alloc(ctx, &mem_47460, bytes_47452, "mem_47460"))
            return 1;
        for (int32_t write_iter_45728 = 0; write_iter_45728 < 6;
             write_iter_45728++) {
            int32_t write_iv_45732 =
                    *(int32_t *) &mem_47443.mem[write_iter_45728 * 4];
            int32_t write_iv_45733 =
                    *(int32_t *) &mem_47440.mem[write_iter_45728 * 4];
            int32_t new_index_46431 = squot32(write_iter_45728, 2);
            int32_t binop_y_46433 = 2 * new_index_46431;
            int32_t new_index_46434 = write_iter_45728 - binop_y_46433;
            bool is_this_one_44001 = write_iv_45732 == 0;
            int32_t this_offset_44002 = -1 + write_iv_45733;
            int32_t total_res_44003;
            
            if (is_this_one_44001) {
                total_res_44003 = this_offset_44002;
            } else {
                total_res_44003 = -1;
            }
            
            bool less_than_zzero_45737 = slt32(total_res_44003, 0);
            bool greater_than_sizze_45738 = sle32(last_offset_43989,
                                                  total_res_44003);
            bool outside_bounds_dim_45739 = less_than_zzero_45737 ||
                 greater_than_sizze_45738;
            
            if (!outside_bounds_dim_45739) {
                memmove(mem_47454.mem + total_res_44003 * 4, mem_47430.mem +
                        (2 * new_index_46431 + new_index_46434) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45739) {
                struct memblock mem_47469;
                
                mem_47469.references = NULL;
                if (memblock_alloc(ctx, &mem_47469, 4, "mem_47469"))
                    return 1;
                
                int32_t x_48320;
                
                for (int32_t i_48319 = 0; i_48319 < 1; i_48319++) {
                    x_48320 = new_index_46434 + sext_i32_i32(i_48319);
                    *(int32_t *) &mem_47469.mem[i_48319 * 4] = x_48320;
                }
                memmove(mem_47457.mem + total_res_44003 * 4, mem_47469.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47469, "mem_47469") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47469, "mem_47469") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45739) {
                struct memblock mem_47472;
                
                mem_47472.references = NULL;
                if (memblock_alloc(ctx, &mem_47472, 4, "mem_47472"))
                    return 1;
                
                int32_t x_48322;
                
                for (int32_t i_48321 = 0; i_48321 < 1; i_48321++) {
                    x_48322 = write_iter_45728 + sext_i32_i32(i_48321);
                    *(int32_t *) &mem_47472.mem[i_48321 * 4] = x_48322;
                }
                memmove(mem_47460.mem + total_res_44003 * 4, mem_47472.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47472, "mem_47472") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47472, "mem_47472") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
            return 1;
        
        bool empty_slice_44004 = last_offset_43989 == 0;
        int32_t m_44005 = last_offset_43989 - 1;
        bool zzero_leq_i_p_m_t_s_44006 = sle32(0, m_44005);
        struct memblock mem_47481;
        
        mem_47481.references = NULL;
        if (memblock_alloc(ctx, &mem_47481, bytes_47452, "mem_47481"))
            return 1;
        
        int32_t tmp_offs_48323 = 0;
        
        memmove(mem_47481.mem + tmp_offs_48323 * 4, mem_46817.mem + 0, 0);
        tmp_offs_48323 = tmp_offs_48323;
        memmove(mem_47481.mem + tmp_offs_48323 * 4, mem_47454.mem + 0,
                last_offset_43989 * sizeof(int32_t));
        tmp_offs_48323 += last_offset_43989;
        
        struct memblock mem_47484;
        
        mem_47484.references = NULL;
        if (memblock_alloc(ctx, &mem_47484, bytes_47452, "mem_47484"))
            return 1;
        
        int32_t tmp_offs_48324 = 0;
        
        memmove(mem_47484.mem + tmp_offs_48324 * 4, mem_46817.mem + 0, 0);
        tmp_offs_48324 = tmp_offs_48324;
        memmove(mem_47484.mem + tmp_offs_48324 * 4, mem_47457.mem + 0,
                last_offset_43989 * sizeof(int32_t));
        tmp_offs_48324 += last_offset_43989;
        
        struct memblock mem_47487;
        
        mem_47487.references = NULL;
        if (memblock_alloc(ctx, &mem_47487, bytes_47452, "mem_47487"))
            return 1;
        
        int32_t tmp_offs_48325 = 0;
        
        memmove(mem_47487.mem + tmp_offs_48325 * 4, mem_46817.mem + 0, 0);
        tmp_offs_48325 = tmp_offs_48325;
        memmove(mem_47487.mem + tmp_offs_48325 * 4, mem_47460.mem + 0,
                last_offset_43989 * sizeof(int32_t));
        tmp_offs_48325 += last_offset_43989;
        
        bool loop_cond_44016 = slt32(1, last_offset_43989);
        int32_t sizze_44017;
        int32_t sizze_44018;
        int32_t sizze_44019;
        int64_t res_mem_sizze_47530;
        struct memblock res_mem_47531;
        
        res_mem_47531.references = NULL;
        
        int64_t res_mem_sizze_47532;
        struct memblock res_mem_47533;
        
        res_mem_47533.references = NULL;
        
        int64_t res_mem_sizze_47534;
        struct memblock res_mem_47535;
        
        res_mem_47535.references = NULL;
        
        int32_t res_44023;
        
        if (empty_slice_44004) {
            struct memblock mem_47490;
            
            mem_47490.references = NULL;
            if (memblock_alloc(ctx, &mem_47490, bytes_47452, "mem_47490"))
                return 1;
            memmove(mem_47490.mem + 0, mem_47481.mem + 0, last_offset_43989 *
                    sizeof(int32_t));
            
            struct memblock mem_47493;
            
            mem_47493.references = NULL;
            if (memblock_alloc(ctx, &mem_47493, bytes_47452, "mem_47493"))
                return 1;
            memmove(mem_47493.mem + 0, mem_47484.mem + 0, last_offset_43989 *
                    sizeof(int32_t));
            
            struct memblock mem_47496;
            
            mem_47496.references = NULL;
            if (memblock_alloc(ctx, &mem_47496, bytes_47452, "mem_47496"))
                return 1;
            memmove(mem_47496.mem + 0, mem_47487.mem + 0, last_offset_43989 *
                    sizeof(int32_t));
            sizze_44017 = last_offset_43989;
            sizze_44018 = last_offset_43989;
            sizze_44019 = last_offset_43989;
            res_mem_sizze_47530 = bytes_47452;
            if (memblock_set(ctx, &res_mem_47531, &mem_47490, "mem_47490") != 0)
                return 1;
            res_mem_sizze_47532 = bytes_47452;
            if (memblock_set(ctx, &res_mem_47533, &mem_47493, "mem_47493") != 0)
                return 1;
            res_mem_sizze_47534 = bytes_47452;
            if (memblock_set(ctx, &res_mem_47535, &mem_47496, "mem_47496") != 0)
                return 1;
            res_44023 = 0;
            if (memblock_unref(ctx, &mem_47496, "mem_47496") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47493, "mem_47493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47490, "mem_47490") != 0)
                return 1;
        } else {
            bool res_44035;
            int32_t res_44036;
            int32_t res_44037;
            bool loop_while_44038;
            int32_t r_44039;
            int32_t n_44040;
            
            loop_while_44038 = loop_cond_44016;
            r_44039 = 0;
            n_44040 = last_offset_43989;
            while (loop_while_44038) {
                int32_t res_44041 = sdiv32(n_44040, 2);
                int32_t res_44042 = 1 + r_44039;
                bool loop_cond_44043 = slt32(1, res_44041);
                bool loop_while_tmp_48326 = loop_cond_44043;
                int32_t r_tmp_48327 = res_44042;
                int32_t n_tmp_48328;
                
                n_tmp_48328 = res_44041;
                loop_while_44038 = loop_while_tmp_48326;
                r_44039 = r_tmp_48327;
                n_44040 = n_tmp_48328;
            }
            res_44035 = loop_while_44038;
            res_44036 = r_44039;
            res_44037 = n_44040;
            
            int32_t y_44044 = 1 << res_44036;
            bool cond_44045 = last_offset_43989 == y_44044;
            int32_t y_44046 = 1 + res_44036;
            int32_t x_44047 = 1 << y_44046;
            int32_t arg_44048 = x_44047 - last_offset_43989;
            bool bounds_invalid_upwards_44049 = slt32(arg_44048, 0);
            int32_t conc_tmp_44050 = last_offset_43989 + arg_44048;
            int32_t sizze_44051;
            
            if (cond_44045) {
                sizze_44051 = last_offset_43989;
            } else {
                sizze_44051 = conc_tmp_44050;
            }
            
            int32_t res_44052;
            
            if (cond_44045) {
                res_44052 = res_44036;
            } else {
                res_44052 = y_44046;
            }
            
            int64_t binop_x_47516 = sext_i32_i64(conc_tmp_44050);
            int64_t bytes_47515 = 4 * binop_x_47516;
            int64_t res_mem_sizze_47524;
            struct memblock res_mem_47525;
            
            res_mem_47525.references = NULL;
            
            int64_t res_mem_sizze_47526;
            struct memblock res_mem_47527;
            
            res_mem_47527.references = NULL;
            
            int64_t res_mem_sizze_47528;
            struct memblock res_mem_47529;
            
            res_mem_47529.references = NULL;
            if (cond_44045) {
                struct memblock mem_47499;
                
                mem_47499.references = NULL;
                if (memblock_alloc(ctx, &mem_47499, bytes_47452, "mem_47499"))
                    return 1;
                memmove(mem_47499.mem + 0, mem_47481.mem + 0,
                        last_offset_43989 * sizeof(int32_t));
                
                struct memblock mem_47502;
                
                mem_47502.references = NULL;
                if (memblock_alloc(ctx, &mem_47502, bytes_47452, "mem_47502"))
                    return 1;
                memmove(mem_47502.mem + 0, mem_47484.mem + 0,
                        last_offset_43989 * sizeof(int32_t));
                
                struct memblock mem_47505;
                
                mem_47505.references = NULL;
                if (memblock_alloc(ctx, &mem_47505, bytes_47452, "mem_47505"))
                    return 1;
                memmove(mem_47505.mem + 0, mem_47487.mem + 0,
                        last_offset_43989 * sizeof(int32_t));
                res_mem_sizze_47524 = bytes_47452;
                if (memblock_set(ctx, &res_mem_47525, &mem_47499,
                                 "mem_47499") != 0)
                    return 1;
                res_mem_sizze_47526 = bytes_47452;
                if (memblock_set(ctx, &res_mem_47527, &mem_47502,
                                 "mem_47502") != 0)
                    return 1;
                res_mem_sizze_47528 = bytes_47452;
                if (memblock_set(ctx, &res_mem_47529, &mem_47505,
                                 "mem_47505") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47505, "mem_47505") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47502, "mem_47502") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47499, "mem_47499") != 0)
                    return 1;
            } else {
                bool y_44070 = slt32(0, last_offset_43989);
                bool index_certs_44071;
                
                if (!y_44070) {
                    ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                           "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                           "Index [", 0,
                                           "] out of bounds for array of shape [",
                                           last_offset_43989, "].");
                    if (memblock_unref(ctx, &res_mem_47529, "res_mem_47529") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47527, "res_mem_47527") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47525, "res_mem_47525") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47379,
                                       "indexed_mem_47379") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47377,
                                       "indexed_mem_47377") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47375,
                                       "indexed_mem_47375") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                        return 1;
                    return 1;
                }
                
                int32_t index_concat_44072 = *(int32_t *) &mem_47454.mem[0];
                int32_t index_concat_44073 = *(int32_t *) &mem_47457.mem[0];
                int32_t index_concat_44074 = *(int32_t *) &mem_47460.mem[0];
                int32_t res_44075;
                int32_t res_44076;
                int32_t res_44077;
                int32_t redout_45755;
                int32_t redout_45756;
                int32_t redout_45757;
                
                redout_45755 = index_concat_44072;
                redout_45756 = index_concat_44073;
                redout_45757 = index_concat_44074;
                for (int32_t i_45758 = 0; i_45758 < last_offset_43989;
                     i_45758++) {
                    bool index_concat_cmp_46461 = sle32(0, i_45758);
                    int32_t index_concat_branch_46465;
                    
                    if (index_concat_cmp_46461) {
                        int32_t index_concat_46463 =
                                *(int32_t *) &mem_47454.mem[i_45758 * 4];
                        
                        index_concat_branch_46465 = index_concat_46463;
                    } else {
                        int32_t index_concat_46464 =
                                *(int32_t *) &mem_46817.mem[i_45758 * 4];
                        
                        index_concat_branch_46465 = index_concat_46464;
                    }
                    
                    int32_t index_concat_branch_46459;
                    
                    if (index_concat_cmp_46461) {
                        int32_t index_concat_46457 =
                                *(int32_t *) &mem_47457.mem[i_45758 * 4];
                        
                        index_concat_branch_46459 = index_concat_46457;
                    } else {
                        int32_t index_concat_46458 =
                                *(int32_t *) &mem_46817.mem[i_45758 * 4];
                        
                        index_concat_branch_46459 = index_concat_46458;
                    }
                    
                    int32_t index_concat_branch_46453;
                    
                    if (index_concat_cmp_46461) {
                        int32_t index_concat_46451 =
                                *(int32_t *) &mem_47460.mem[i_45758 * 4];
                        
                        index_concat_branch_46453 = index_concat_46451;
                    } else {
                        int32_t index_concat_46452 =
                                *(int32_t *) &mem_46817.mem[i_45758 * 4];
                        
                        index_concat_branch_46453 = index_concat_46452;
                    }
                    
                    bool cond_44084 = redout_45755 == index_concat_branch_46465;
                    bool res_44085 = sle32(redout_45756,
                                           index_concat_branch_46459);
                    bool res_44086 = sle32(redout_45755,
                                           index_concat_branch_46465);
                    bool x_44087 = cond_44084 && res_44085;
                    bool x_44088 = !cond_44084;
                    bool y_44089 = res_44086 && x_44088;
                    bool res_44090 = x_44087 || y_44089;
                    int32_t res_44091;
                    
                    if (res_44090) {
                        res_44091 = index_concat_branch_46465;
                    } else {
                        res_44091 = redout_45755;
                    }
                    
                    int32_t res_44092;
                    
                    if (res_44090) {
                        res_44092 = index_concat_branch_46459;
                    } else {
                        res_44092 = redout_45756;
                    }
                    
                    int32_t res_44093;
                    
                    if (res_44090) {
                        res_44093 = index_concat_branch_46453;
                    } else {
                        res_44093 = redout_45757;
                    }
                    
                    int32_t redout_tmp_48329 = res_44091;
                    int32_t redout_tmp_48330 = res_44092;
                    int32_t redout_tmp_48331;
                    
                    redout_tmp_48331 = res_44093;
                    redout_45755 = redout_tmp_48329;
                    redout_45756 = redout_tmp_48330;
                    redout_45757 = redout_tmp_48331;
                }
                res_44075 = redout_45755;
                res_44076 = redout_45756;
                res_44077 = redout_45757;
                
                bool eq_x_zz_44097 = 0 == arg_44048;
                bool not_p_44098 = !bounds_invalid_upwards_44049;
                bool p_and_eq_x_y_44099 = eq_x_zz_44097 && not_p_44098;
                bool dim_zzero_44100 = bounds_invalid_upwards_44049 ||
                     p_and_eq_x_y_44099;
                bool both_empty_44101 = eq_x_zz_44097 && dim_zzero_44100;
                bool eq_x_y_44102 = arg_44048 == 0;
                bool p_and_eq_x_y_44103 = bounds_invalid_upwards_44049 &&
                     eq_x_y_44102;
                bool dim_match_44104 = not_p_44098 || p_and_eq_x_y_44103;
                bool empty_or_match_44105 = both_empty_44101 || dim_match_44104;
                bool empty_or_match_cert_44106;
                
                if (!empty_or_match_44105) {
                    ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                           "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                           "Function return value does not match shape of type ",
                                           "*", "[", arg_44048, "]", "t");
                    if (memblock_unref(ctx, &res_mem_47529, "res_mem_47529") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47527, "res_mem_47527") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47525, "res_mem_47525") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47379,
                                       "indexed_mem_47379") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47377,
                                       "indexed_mem_47377") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47375,
                                       "indexed_mem_47375") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                        return 1;
                    return 1;
                }
                
                int64_t binop_x_47507 = sext_i32_i64(arg_44048);
                int64_t bytes_47506 = 4 * binop_x_47507;
                struct memblock mem_47508;
                
                mem_47508.references = NULL;
                if (memblock_alloc(ctx, &mem_47508, bytes_47506, "mem_47508"))
                    return 1;
                for (int32_t i_48332 = 0; i_48332 < arg_44048; i_48332++) {
                    *(int32_t *) &mem_47508.mem[i_48332 * 4] = res_44075;
                }
                
                struct memblock mem_47511;
                
                mem_47511.references = NULL;
                if (memblock_alloc(ctx, &mem_47511, bytes_47506, "mem_47511"))
                    return 1;
                for (int32_t i_48333 = 0; i_48333 < arg_44048; i_48333++) {
                    *(int32_t *) &mem_47511.mem[i_48333 * 4] = res_44076;
                }
                
                struct memblock mem_47514;
                
                mem_47514.references = NULL;
                if (memblock_alloc(ctx, &mem_47514, bytes_47506, "mem_47514"))
                    return 1;
                for (int32_t i_48334 = 0; i_48334 < arg_44048; i_48334++) {
                    *(int32_t *) &mem_47514.mem[i_48334 * 4] = res_44077;
                }
                
                struct memblock mem_47517;
                
                mem_47517.references = NULL;
                if (memblock_alloc(ctx, &mem_47517, bytes_47515, "mem_47517"))
                    return 1;
                
                int32_t tmp_offs_48335 = 0;
                
                memmove(mem_47517.mem + tmp_offs_48335 * 4, mem_46817.mem + 0,
                        0);
                tmp_offs_48335 = tmp_offs_48335;
                memmove(mem_47517.mem + tmp_offs_48335 * 4, mem_47454.mem + 0,
                        last_offset_43989 * sizeof(int32_t));
                tmp_offs_48335 += last_offset_43989;
                memmove(mem_47517.mem + tmp_offs_48335 * 4, mem_47508.mem + 0,
                        arg_44048 * sizeof(int32_t));
                tmp_offs_48335 += arg_44048;
                if (memblock_unref(ctx, &mem_47508, "mem_47508") != 0)
                    return 1;
                
                struct memblock mem_47520;
                
                mem_47520.references = NULL;
                if (memblock_alloc(ctx, &mem_47520, bytes_47515, "mem_47520"))
                    return 1;
                
                int32_t tmp_offs_48336 = 0;
                
                memmove(mem_47520.mem + tmp_offs_48336 * 4, mem_46817.mem + 0,
                        0);
                tmp_offs_48336 = tmp_offs_48336;
                memmove(mem_47520.mem + tmp_offs_48336 * 4, mem_47457.mem + 0,
                        last_offset_43989 * sizeof(int32_t));
                tmp_offs_48336 += last_offset_43989;
                memmove(mem_47520.mem + tmp_offs_48336 * 4, mem_47511.mem + 0,
                        arg_44048 * sizeof(int32_t));
                tmp_offs_48336 += arg_44048;
                if (memblock_unref(ctx, &mem_47511, "mem_47511") != 0)
                    return 1;
                
                struct memblock mem_47523;
                
                mem_47523.references = NULL;
                if (memblock_alloc(ctx, &mem_47523, bytes_47515, "mem_47523"))
                    return 1;
                
                int32_t tmp_offs_48337 = 0;
                
                memmove(mem_47523.mem + tmp_offs_48337 * 4, mem_46817.mem + 0,
                        0);
                tmp_offs_48337 = tmp_offs_48337;
                memmove(mem_47523.mem + tmp_offs_48337 * 4, mem_47460.mem + 0,
                        last_offset_43989 * sizeof(int32_t));
                tmp_offs_48337 += last_offset_43989;
                memmove(mem_47523.mem + tmp_offs_48337 * 4, mem_47514.mem + 0,
                        arg_44048 * sizeof(int32_t));
                tmp_offs_48337 += arg_44048;
                if (memblock_unref(ctx, &mem_47514, "mem_47514") != 0)
                    return 1;
                res_mem_sizze_47524 = bytes_47515;
                if (memblock_set(ctx, &res_mem_47525, &mem_47517,
                                 "mem_47517") != 0)
                    return 1;
                res_mem_sizze_47526 = bytes_47515;
                if (memblock_set(ctx, &res_mem_47527, &mem_47520,
                                 "mem_47520") != 0)
                    return 1;
                res_mem_sizze_47528 = bytes_47515;
                if (memblock_set(ctx, &res_mem_47529, &mem_47523,
                                 "mem_47523") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47523, "mem_47523") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47520, "mem_47520") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47517, "mem_47517") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47514, "mem_47514") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47511, "mem_47511") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47508, "mem_47508") != 0)
                    return 1;
            }
            sizze_44017 = sizze_44051;
            sizze_44018 = sizze_44051;
            sizze_44019 = sizze_44051;
            res_mem_sizze_47530 = res_mem_sizze_47524;
            if (memblock_set(ctx, &res_mem_47531, &res_mem_47525,
                             "res_mem_47525") != 0)
                return 1;
            res_mem_sizze_47532 = res_mem_sizze_47526;
            if (memblock_set(ctx, &res_mem_47533, &res_mem_47527,
                             "res_mem_47527") != 0)
                return 1;
            res_mem_sizze_47534 = res_mem_sizze_47528;
            if (memblock_set(ctx, &res_mem_47535, &res_mem_47529,
                             "res_mem_47529") != 0)
                return 1;
            res_44023 = res_44052;
            if (memblock_unref(ctx, &res_mem_47529, "res_mem_47529") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47527, "res_mem_47527") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47525, "res_mem_47525") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
            return 1;
        
        bool dim_zzero_44116 = 0 == sizze_44018;
        bool dim_zzero_44117 = 0 == sizze_44017;
        bool both_empty_44118 = dim_zzero_44116 && dim_zzero_44117;
        bool dim_match_44119 = sizze_44017 == sizze_44018;
        bool empty_or_match_44120 = both_empty_44118 || dim_match_44119;
        bool empty_or_match_cert_44121;
        
        if (!empty_or_match_44120) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        bool dim_zzero_44123 = 0 == sizze_44019;
        bool both_empty_44124 = dim_zzero_44117 && dim_zzero_44123;
        bool dim_match_44125 = sizze_44017 == sizze_44019;
        bool empty_or_match_44126 = both_empty_44124 || dim_match_44125;
        bool empty_or_match_cert_44127;
        
        if (!empty_or_match_44126) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_47549 = sext_i32_i64(sizze_44017);
        int64_t bytes_47548 = 4 * binop_x_47549;
        int64_t indexed_mem_sizze_47575;
        struct memblock indexed_mem_47576;
        
        indexed_mem_47576.references = NULL;
        
        int64_t indexed_mem_sizze_47577;
        struct memblock indexed_mem_47578;
        
        indexed_mem_47578.references = NULL;
        
        int64_t indexed_mem_sizze_47579;
        struct memblock indexed_mem_47580;
        
        indexed_mem_47580.references = NULL;
        
        int64_t xs_mem_sizze_47536;
        struct memblock xs_mem_47537;
        
        xs_mem_47537.references = NULL;
        
        int64_t xs_mem_sizze_47538;
        struct memblock xs_mem_47539;
        
        xs_mem_47539.references = NULL;
        
        int64_t xs_mem_sizze_47540;
        struct memblock xs_mem_47541;
        
        xs_mem_47541.references = NULL;
        xs_mem_sizze_47536 = res_mem_sizze_47530;
        if (memblock_set(ctx, &xs_mem_47537, &res_mem_47531, "res_mem_47531") !=
            0)
            return 1;
        xs_mem_sizze_47538 = res_mem_sizze_47532;
        if (memblock_set(ctx, &xs_mem_47539, &res_mem_47533, "res_mem_47533") !=
            0)
            return 1;
        xs_mem_sizze_47540 = res_mem_sizze_47534;
        if (memblock_set(ctx, &xs_mem_47541, &res_mem_47535, "res_mem_47535") !=
            0)
            return 1;
        for (int32_t i_44144 = 0; i_44144 < res_44023; i_44144++) {
            int32_t upper_bound_44145 = 1 + i_44144;
            int64_t res_mem_sizze_47569;
            struct memblock res_mem_47570;
            
            res_mem_47570.references = NULL;
            
            int64_t res_mem_sizze_47571;
            struct memblock res_mem_47572;
            
            res_mem_47572.references = NULL;
            
            int64_t res_mem_sizze_47573;
            struct memblock res_mem_47574;
            
            res_mem_47574.references = NULL;
            
            int64_t xs_mem_sizze_47542;
            struct memblock xs_mem_47543;
            
            xs_mem_47543.references = NULL;
            
            int64_t xs_mem_sizze_47544;
            struct memblock xs_mem_47545;
            
            xs_mem_47545.references = NULL;
            
            int64_t xs_mem_sizze_47546;
            struct memblock xs_mem_47547;
            
            xs_mem_47547.references = NULL;
            xs_mem_sizze_47542 = xs_mem_sizze_47536;
            if (memblock_set(ctx, &xs_mem_47543, &xs_mem_47537,
                             "xs_mem_47537") != 0)
                return 1;
            xs_mem_sizze_47544 = xs_mem_sizze_47538;
            if (memblock_set(ctx, &xs_mem_47545, &xs_mem_47539,
                             "xs_mem_47539") != 0)
                return 1;
            xs_mem_sizze_47546 = xs_mem_sizze_47540;
            if (memblock_set(ctx, &xs_mem_47547, &xs_mem_47541,
                             "xs_mem_47541") != 0)
                return 1;
            for (int32_t j_44156 = 0; j_44156 < upper_bound_44145; j_44156++) {
                int32_t y_44157 = i_44144 - j_44156;
                int32_t res_44158 = 1 << y_44157;
                struct memblock mem_47550;
                
                mem_47550.references = NULL;
                if (memblock_alloc(ctx, &mem_47550, bytes_47548, "mem_47550"))
                    return 1;
                
                struct memblock mem_47553;
                
                mem_47553.references = NULL;
                if (memblock_alloc(ctx, &mem_47553, bytes_47548, "mem_47553"))
                    return 1;
                
                struct memblock mem_47556;
                
                mem_47556.references = NULL;
                if (memblock_alloc(ctx, &mem_47556, bytes_47548, "mem_47556"))
                    return 1;
                for (int32_t i_45765 = 0; i_45765 < sizze_44017; i_45765++) {
                    int32_t res_44163 = *(int32_t *) &xs_mem_47543.mem[i_45765 *
                                                                       4];
                    int32_t res_44164 = *(int32_t *) &xs_mem_47545.mem[i_45765 *
                                                                       4];
                    int32_t res_44165 = *(int32_t *) &xs_mem_47547.mem[i_45765 *
                                                                       4];
                    int32_t x_44166 = ashr32(i_45765, i_44144);
                    int32_t x_44167 = 2 & x_44166;
                    bool res_44168 = x_44167 == 0;
                    int32_t x_44169 = res_44158 & i_45765;
                    bool cond_44170 = x_44169 == 0;
                    int32_t res_44171;
                    int32_t res_44172;
                    int32_t res_44173;
                    
                    if (cond_44170) {
                        int32_t i_44174 = res_44158 | i_45765;
                        int32_t res_44175 =
                                *(int32_t *) &xs_mem_47543.mem[i_44174 * 4];
                        int32_t res_44176 =
                                *(int32_t *) &xs_mem_47545.mem[i_44174 * 4];
                        int32_t res_44177 =
                                *(int32_t *) &xs_mem_47547.mem[i_44174 * 4];
                        bool cond_44178 = res_44175 == res_44163;
                        bool res_44179 = sle32(res_44176, res_44164);
                        bool res_44180 = sle32(res_44175, res_44163);
                        bool x_44181 = cond_44178 && res_44179;
                        bool x_44182 = !cond_44178;
                        bool y_44183 = res_44180 && x_44182;
                        bool res_44184 = x_44181 || y_44183;
                        bool cond_44185 = res_44184 == res_44168;
                        int32_t res_44186;
                        
                        if (cond_44185) {
                            res_44186 = res_44175;
                        } else {
                            res_44186 = res_44163;
                        }
                        
                        int32_t res_44187;
                        
                        if (cond_44185) {
                            res_44187 = res_44176;
                        } else {
                            res_44187 = res_44164;
                        }
                        
                        int32_t res_44188;
                        
                        if (cond_44185) {
                            res_44188 = res_44177;
                        } else {
                            res_44188 = res_44165;
                        }
                        res_44171 = res_44186;
                        res_44172 = res_44187;
                        res_44173 = res_44188;
                    } else {
                        int32_t i_44189 = res_44158 ^ i_45765;
                        int32_t res_44190 =
                                *(int32_t *) &xs_mem_47543.mem[i_44189 * 4];
                        int32_t res_44191 =
                                *(int32_t *) &xs_mem_47545.mem[i_44189 * 4];
                        int32_t res_44192 =
                                *(int32_t *) &xs_mem_47547.mem[i_44189 * 4];
                        bool cond_44193 = res_44163 == res_44190;
                        bool res_44194 = sle32(res_44164, res_44191);
                        bool res_44195 = sle32(res_44163, res_44190);
                        bool x_44196 = cond_44193 && res_44194;
                        bool x_44197 = !cond_44193;
                        bool y_44198 = res_44195 && x_44197;
                        bool res_44199 = x_44196 || y_44198;
                        bool cond_44200 = res_44199 == res_44168;
                        int32_t res_44201;
                        
                        if (cond_44200) {
                            res_44201 = res_44190;
                        } else {
                            res_44201 = res_44163;
                        }
                        
                        int32_t res_44202;
                        
                        if (cond_44200) {
                            res_44202 = res_44191;
                        } else {
                            res_44202 = res_44164;
                        }
                        
                        int32_t res_44203;
                        
                        if (cond_44200) {
                            res_44203 = res_44192;
                        } else {
                            res_44203 = res_44165;
                        }
                        res_44171 = res_44201;
                        res_44172 = res_44202;
                        res_44173 = res_44203;
                    }
                    *(int32_t *) &mem_47550.mem[i_45765 * 4] = res_44171;
                    *(int32_t *) &mem_47553.mem[i_45765 * 4] = res_44172;
                    *(int32_t *) &mem_47556.mem[i_45765 * 4] = res_44173;
                }
                
                int64_t xs_mem_sizze_tmp_48347 = bytes_47548;
                struct memblock xs_mem_tmp_48348;
                
                xs_mem_tmp_48348.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48348, &mem_47550,
                                 "mem_47550") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_48349 = bytes_47548;
                struct memblock xs_mem_tmp_48350;
                
                xs_mem_tmp_48350.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48350, &mem_47553,
                                 "mem_47553") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_48351 = bytes_47548;
                struct memblock xs_mem_tmp_48352;
                
                xs_mem_tmp_48352.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48352, &mem_47556,
                                 "mem_47556") != 0)
                    return 1;
                xs_mem_sizze_47542 = xs_mem_sizze_tmp_48347;
                if (memblock_set(ctx, &xs_mem_47543, &xs_mem_tmp_48348,
                                 "xs_mem_tmp_48348") != 0)
                    return 1;
                xs_mem_sizze_47544 = xs_mem_sizze_tmp_48349;
                if (memblock_set(ctx, &xs_mem_47545, &xs_mem_tmp_48350,
                                 "xs_mem_tmp_48350") != 0)
                    return 1;
                xs_mem_sizze_47546 = xs_mem_sizze_tmp_48351;
                if (memblock_set(ctx, &xs_mem_47547, &xs_mem_tmp_48352,
                                 "xs_mem_tmp_48352") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48352,
                                   "xs_mem_tmp_48352") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48350,
                                   "xs_mem_tmp_48350") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48348,
                                   "xs_mem_tmp_48348") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47556, "mem_47556") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47553, "mem_47553") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47550, "mem_47550") != 0)
                    return 1;
            }
            res_mem_sizze_47569 = xs_mem_sizze_47542;
            if (memblock_set(ctx, &res_mem_47570, &xs_mem_47543,
                             "xs_mem_47543") != 0)
                return 1;
            res_mem_sizze_47571 = xs_mem_sizze_47544;
            if (memblock_set(ctx, &res_mem_47572, &xs_mem_47545,
                             "xs_mem_47545") != 0)
                return 1;
            res_mem_sizze_47573 = xs_mem_sizze_47546;
            if (memblock_set(ctx, &res_mem_47574, &xs_mem_47547,
                             "xs_mem_47547") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48338 = res_mem_sizze_47569;
            struct memblock xs_mem_tmp_48339;
            
            xs_mem_tmp_48339.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48339, &res_mem_47570,
                             "res_mem_47570") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48340 = res_mem_sizze_47571;
            struct memblock xs_mem_tmp_48341;
            
            xs_mem_tmp_48341.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48341, &res_mem_47572,
                             "res_mem_47572") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48342 = res_mem_sizze_47573;
            struct memblock xs_mem_tmp_48343;
            
            xs_mem_tmp_48343.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48343, &res_mem_47574,
                             "res_mem_47574") != 0)
                return 1;
            xs_mem_sizze_47536 = xs_mem_sizze_tmp_48338;
            if (memblock_set(ctx, &xs_mem_47537, &xs_mem_tmp_48339,
                             "xs_mem_tmp_48339") != 0)
                return 1;
            xs_mem_sizze_47538 = xs_mem_sizze_tmp_48340;
            if (memblock_set(ctx, &xs_mem_47539, &xs_mem_tmp_48341,
                             "xs_mem_tmp_48341") != 0)
                return 1;
            xs_mem_sizze_47540 = xs_mem_sizze_tmp_48342;
            if (memblock_set(ctx, &xs_mem_47541, &xs_mem_tmp_48343,
                             "xs_mem_tmp_48343") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48343, "xs_mem_tmp_48343") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48341, "xs_mem_tmp_48341") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48339, "xs_mem_tmp_48339") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47547, "xs_mem_47547") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47545, "xs_mem_47545") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47543, "xs_mem_47543") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47574, "res_mem_47574") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47572, "res_mem_47572") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47570, "res_mem_47570") != 0)
                return 1;
        }
        indexed_mem_sizze_47575 = xs_mem_sizze_47536;
        if (memblock_set(ctx, &indexed_mem_47576, &xs_mem_47537,
                         "xs_mem_47537") != 0)
            return 1;
        indexed_mem_sizze_47577 = xs_mem_sizze_47538;
        if (memblock_set(ctx, &indexed_mem_47578, &xs_mem_47539,
                         "xs_mem_47539") != 0)
            return 1;
        indexed_mem_sizze_47579 = xs_mem_sizze_47540;
        if (memblock_set(ctx, &indexed_mem_47580, &xs_mem_47541,
                         "xs_mem_47541") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
            return 1;
        
        bool i_p_m_t_s_leq_w_44204 = slt32(m_44005, sizze_44017);
        bool y_44205 = zzero_leq_i_p_m_t_s_44006 && i_p_m_t_s_leq_w_44204;
        bool ok_or_empty_44207 = empty_slice_44004 || y_44205;
        bool index_certs_44208;
        
        if (!ok_or_empty_44207) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                                   "Index [", "", ":", last_offset_43989,
                                   "] out of bounds for array of shape [",
                                   sizze_44017, "].");
            if (memblock_unref(ctx, &xs_mem_47541, "xs_mem_47541") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47539, "xs_mem_47539") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47537, "xs_mem_47537") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47580, "indexed_mem_47580") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47578, "indexed_mem_47578") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47576, "indexed_mem_47576") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        struct memblock mem_47583;
        
        mem_47583.references = NULL;
        if (memblock_alloc(ctx, &mem_47583, bytes_47452, "mem_47583"))
            return 1;
        
        struct memblock mem_47585;
        
        mem_47585.references = NULL;
        if (memblock_alloc(ctx, &mem_47585, binop_x_47453, "mem_47585"))
            return 1;
        
        int32_t discard_45792;
        int32_t scanacc_45777 = 1;
        
        for (int32_t i_45783 = 0; i_45783 < last_offset_43989; i_45783++) {
            int32_t x_44230 = *(int32_t *) &indexed_mem_47576.mem[i_45783 * 4];
            int32_t x_44231 = *(int32_t *) &indexed_mem_47578.mem[i_45783 * 4];
            int32_t i_p_o_46479 = -1 + i_45783;
            int32_t rot_i_46480 = smod32(i_p_o_46479, last_offset_43989);
            int32_t x_44232 = *(int32_t *) &indexed_mem_47576.mem[rot_i_46480 *
                                                                  4];
            int32_t x_44233 = *(int32_t *) &indexed_mem_47578.mem[rot_i_46480 *
                                                                  4];
            int32_t x_44234 = *(int32_t *) &indexed_mem_47580.mem[i_45783 * 4];
            bool res_44235 = x_44230 == x_44232;
            bool res_44236 = x_44231 == x_44233;
            bool eq_44237 = res_44235 && res_44236;
            bool res_44238 = !eq_44237;
            int32_t res_44228;
            
            if (res_44238) {
                res_44228 = x_44234;
            } else {
                int32_t res_44229 = x_44234 * scanacc_45777;
                
                res_44228 = res_44229;
            }
            *(int32_t *) &mem_47583.mem[i_45783 * 4] = res_44228;
            *(bool *) &mem_47585.mem[i_45783] = res_44238;
            
            int32_t scanacc_tmp_48359 = res_44228;
            
            scanacc_45777 = scanacc_tmp_48359;
        }
        discard_45792 = scanacc_45777;
        if (memblock_unref(ctx, &indexed_mem_47576, "indexed_mem_47576") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47578, "indexed_mem_47578") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47580, "indexed_mem_47580") != 0)
            return 1;
        
        struct memblock mem_47596;
        
        mem_47596.references = NULL;
        if (memblock_alloc(ctx, &mem_47596, bytes_47452, "mem_47596"))
            return 1;
        
        int32_t discard_45798;
        int32_t scanacc_45794 = 0;
        
        for (int32_t i_45796 = 0; i_45796 < last_offset_43989; i_45796++) {
            int32_t i_p_o_46487 = 1 + i_45796;
            int32_t rot_i_46488 = smod32(i_p_o_46487, last_offset_43989);
            bool x_44244 = *(bool *) &mem_47585.mem[rot_i_46488];
            int32_t res_44245 = btoi_bool_i32(x_44244);
            int32_t res_44243 = res_44245 + scanacc_45794;
            
            *(int32_t *) &mem_47596.mem[i_45796 * 4] = res_44243;
            
            int32_t scanacc_tmp_48362 = res_44243;
            
            scanacc_45794 = scanacc_tmp_48362;
        }
        discard_45798 = scanacc_45794;
        
        int32_t res_44246;
        
        if (loop_cond_44016) {
            bool index_certs_44247;
            
            if (!zzero_leq_i_p_m_t_s_44006) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                       "Index [", m_44005,
                                       "] out of bounds for array of shape [",
                                       last_offset_43989, "].");
                if (memblock_unref(ctx, &mem_47596, "mem_47596") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47585, "mem_47585") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47583, "mem_47583") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47541, "xs_mem_47541") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47539, "xs_mem_47539") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47537, "xs_mem_47537") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47580,
                                   "indexed_mem_47580") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47578,
                                   "indexed_mem_47578") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47576,
                                   "indexed_mem_47576") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47379,
                                   "indexed_mem_47379") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47377,
                                   "indexed_mem_47377") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47375,
                                   "indexed_mem_47375") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                    return 1;
                return 1;
            }
            
            int32_t res_44248 = *(int32_t *) &mem_47596.mem[m_44005 * 4];
            
            res_44246 = res_44248;
        } else {
            res_44246 = 0;
        }
        
        bool bounds_invalid_upwards_44249 = slt32(res_44246, 0);
        bool eq_x_zz_44250 = 0 == res_44246;
        bool not_p_44251 = !bounds_invalid_upwards_44249;
        bool p_and_eq_x_y_44252 = eq_x_zz_44250 && not_p_44251;
        bool dim_zzero_44253 = bounds_invalid_upwards_44249 ||
             p_and_eq_x_y_44252;
        bool both_empty_44254 = eq_x_zz_44250 && dim_zzero_44253;
        bool eq_x_y_44255 = res_44246 == 0;
        bool empty_or_match_44258 = not_p_44251 || both_empty_44254;
        bool empty_or_match_cert_44259;
        
        if (!empty_or_match_44258) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                                   "Function return value does not match shape of type ",
                                   "*", "[", res_44246, "]", "t");
            if (memblock_unref(ctx, &mem_47596, "mem_47596") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47585, "mem_47585") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47583, "mem_47583") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47541, "xs_mem_47541") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47539, "xs_mem_47539") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47537, "xs_mem_47537") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47580, "indexed_mem_47580") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47578, "indexed_mem_47578") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47576, "indexed_mem_47576") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_47602 = sext_i32_i64(res_44246);
        int64_t bytes_47601 = 4 * binop_x_47602;
        struct memblock mem_47603;
        
        mem_47603.references = NULL;
        if (memblock_alloc(ctx, &mem_47603, bytes_47601, "mem_47603"))
            return 1;
        for (int32_t i_48364 = 0; i_48364 < res_44246; i_48364++) {
            *(int32_t *) &mem_47603.mem[i_48364 * 4] = 1;
        }
        for (int32_t write_iter_45799 = 0; write_iter_45799 < last_offset_43989;
             write_iter_45799++) {
            int32_t write_iv_45803 =
                    *(int32_t *) &mem_47596.mem[write_iter_45799 * 4];
            int32_t i_p_o_46492 = 1 + write_iter_45799;
            int32_t rot_i_46493 = smod32(i_p_o_46492, last_offset_43989);
            bool write_iv_45804 = *(bool *) &mem_47585.mem[rot_i_46493];
            int32_t res_44271;
            
            if (write_iv_45804) {
                int32_t res_44272 = write_iv_45803 - 1;
                
                res_44271 = res_44272;
            } else {
                res_44271 = -1;
            }
            
            bool less_than_zzero_45820 = slt32(res_44271, 0);
            bool greater_than_sizze_45821 = sle32(res_44246, res_44271);
            bool outside_bounds_dim_45822 = less_than_zzero_45820 ||
                 greater_than_sizze_45821;
            
            if (!outside_bounds_dim_45822) {
                memmove(mem_47603.mem + res_44271 * 4, mem_47583.mem +
                        write_iter_45799 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47583, "mem_47583") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47585, "mem_47585") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47596, "mem_47596") != 0)
            return 1;
        
        struct memblock mem_47610;
        
        mem_47610.references = NULL;
        if (memblock_alloc(ctx, &mem_47610, bytes_47601, "mem_47610"))
            return 1;
        
        struct memblock mem_47613;
        
        mem_47613.references = NULL;
        if (memblock_alloc(ctx, &mem_47613, bytes_47601, "mem_47613"))
            return 1;
        
        int32_t discard_45834;
        int32_t scanacc_45828 = 0;
        
        for (int32_t i_45831 = 0; i_45831 < res_44246; i_45831++) {
            int32_t x_44278 = *(int32_t *) &mem_47603.mem[i_45831 * 4];
            bool not_arg_44279 = x_44278 == 0;
            bool res_44280 = !not_arg_44279;
            int32_t part_res_44281;
            
            if (res_44280) {
                part_res_44281 = 0;
            } else {
                part_res_44281 = 1;
            }
            
            int32_t part_res_44282;
            
            if (res_44280) {
                part_res_44282 = 1;
            } else {
                part_res_44282 = 0;
            }
            
            int32_t zz_44277 = part_res_44282 + scanacc_45828;
            
            *(int32_t *) &mem_47610.mem[i_45831 * 4] = zz_44277;
            *(int32_t *) &mem_47613.mem[i_45831 * 4] = part_res_44281;
            
            int32_t scanacc_tmp_48366 = zz_44277;
            
            scanacc_45828 = scanacc_tmp_48366;
        }
        discard_45834 = scanacc_45828;
        
        int32_t last_index_44283 = res_44246 - 1;
        int32_t partition_sizze_44284;
        
        if (eq_x_y_44255) {
            partition_sizze_44284 = 0;
        } else {
            int32_t last_offset_44285 =
                    *(int32_t *) &mem_47610.mem[last_index_44283 * 4];
            
            partition_sizze_44284 = last_offset_44285;
        }
        
        int64_t binop_x_47623 = sext_i32_i64(partition_sizze_44284);
        int64_t bytes_47622 = 4 * binop_x_47623;
        struct memblock mem_47624;
        
        mem_47624.references = NULL;
        if (memblock_alloc(ctx, &mem_47624, bytes_47622, "mem_47624"))
            return 1;
        for (int32_t write_iter_45835 = 0; write_iter_45835 < res_44246;
             write_iter_45835++) {
            int32_t write_iv_45837 =
                    *(int32_t *) &mem_47613.mem[write_iter_45835 * 4];
            int32_t write_iv_45838 =
                    *(int32_t *) &mem_47610.mem[write_iter_45835 * 4];
            bool is_this_one_44293 = write_iv_45837 == 0;
            int32_t this_offset_44294 = -1 + write_iv_45838;
            int32_t total_res_44295;
            
            if (is_this_one_44293) {
                total_res_44295 = this_offset_44294;
            } else {
                total_res_44295 = -1;
            }
            
            bool less_than_zzero_45842 = slt32(total_res_44295, 0);
            bool greater_than_sizze_45843 = sle32(partition_sizze_44284,
                                                  total_res_44295);
            bool outside_bounds_dim_45844 = less_than_zzero_45842 ||
                 greater_than_sizze_45843;
            
            if (!outside_bounds_dim_45844) {
                memmove(mem_47624.mem + total_res_44295 * 4, mem_47603.mem +
                        write_iter_45835 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47603, "mem_47603") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47610, "mem_47610") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47613, "mem_47613") != 0)
            return 1;
        
        bool dim_eq_44296 = partition_sizze_44284 == last_offset_43989;
        bool arrays_equal_44297;
        
        if (dim_eq_44296) {
            bool all_equal_44299;
            bool redout_45848 = 1;
            
            for (int32_t i_45849 = 0; i_45849 < partition_sizze_44284;
                 i_45849++) {
                int32_t x_44303 = *(int32_t *) &mem_47624.mem[i_45849 * 4];
                int32_t y_44304 = *(int32_t *) &mem_47460.mem[i_45849 * 4];
                bool res_44305 = x_44303 == y_44304;
                bool res_44302 = res_44305 && redout_45848;
                bool redout_tmp_48370 = res_44302;
                
                redout_45848 = redout_tmp_48370;
            }
            all_equal_44299 = redout_45848;
            arrays_equal_44297 = all_equal_44299;
        } else {
            arrays_equal_44297 = 0;
        }
        if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47624, "mem_47624") != 0)
            return 1;
        cond_43974 = arrays_equal_44297;
        if (memblock_unref(ctx, &mem_47624, "mem_47624") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47613, "mem_47613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47610, "mem_47610") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47603, "mem_47603") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47596, "mem_47596") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47585, "mem_47585") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47583, "mem_47583") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47541, "xs_mem_47541") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47539, "xs_mem_47539") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47537, "xs_mem_47537") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47580, "indexed_mem_47580") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47578, "indexed_mem_47578") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47576, "indexed_mem_47576") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47535, "res_mem_47535") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47533, "res_mem_47533") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47531, "res_mem_47531") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47487, "mem_47487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47484, "mem_47484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47481, "mem_47481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47460, "mem_47460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47457, "mem_47457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47454, "mem_47454") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47443, "mem_47443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47440, "mem_47440") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47435, "mem_47435") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47430, "mem_47430") != 0)
            return 1;
    } else {
        cond_43974 = 0;
    }
    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
        return 1;
    
    bool cond_44306;
    
    if (cond_43974) {
        struct memblock mem_47631;
        
        mem_47631.references = NULL;
        if (memblock_alloc(ctx, &mem_47631, 24, "mem_47631"))
            return 1;
        
        struct memblock mem_47636;
        
        mem_47636.references = NULL;
        if (memblock_alloc(ctx, &mem_47636, 8, "mem_47636"))
            return 1;
        for (int32_t i_45852 = 0; i_45852 < 3; i_45852++) {
            for (int32_t i_48372 = 0; i_48372 < 2; i_48372++) {
                *(int32_t *) &mem_47636.mem[i_48372 * 4] = i_45852;
            }
            memmove(mem_47631.mem + 2 * i_45852 * 4, mem_47636.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
            return 1;
        
        struct memblock mem_47641;
        
        mem_47641.references = NULL;
        if (memblock_alloc(ctx, &mem_47641, 24, "mem_47641"))
            return 1;
        
        struct memblock mem_47644;
        
        mem_47644.references = NULL;
        if (memblock_alloc(ctx, &mem_47644, 24, "mem_47644"))
            return 1;
        
        int32_t discard_45862;
        int32_t scanacc_45856 = 0;
        
        for (int32_t i_45859 = 0; i_45859 < 6; i_45859++) {
            bool not_arg_44317 = i_45859 == 0;
            bool res_44318 = !not_arg_44317;
            int32_t part_res_44319;
            
            if (res_44318) {
                part_res_44319 = 0;
            } else {
                part_res_44319 = 1;
            }
            
            int32_t part_res_44320;
            
            if (res_44318) {
                part_res_44320 = 1;
            } else {
                part_res_44320 = 0;
            }
            
            int32_t zz_44315 = part_res_44320 + scanacc_45856;
            
            *(int32_t *) &mem_47641.mem[i_45859 * 4] = zz_44315;
            *(int32_t *) &mem_47644.mem[i_45859 * 4] = part_res_44319;
            
            int32_t scanacc_tmp_48373 = zz_44315;
            
            scanacc_45856 = scanacc_tmp_48373;
        }
        discard_45862 = scanacc_45856;
        
        int32_t last_offset_44321 = *(int32_t *) &mem_47641.mem[20];
        int64_t binop_x_47654 = sext_i32_i64(last_offset_44321);
        int64_t bytes_47653 = 4 * binop_x_47654;
        struct memblock mem_47655;
        
        mem_47655.references = NULL;
        if (memblock_alloc(ctx, &mem_47655, bytes_47653, "mem_47655"))
            return 1;
        
        struct memblock mem_47658;
        
        mem_47658.references = NULL;
        if (memblock_alloc(ctx, &mem_47658, bytes_47653, "mem_47658"))
            return 1;
        
        struct memblock mem_47661;
        
        mem_47661.references = NULL;
        if (memblock_alloc(ctx, &mem_47661, bytes_47653, "mem_47661"))
            return 1;
        for (int32_t write_iter_45863 = 0; write_iter_45863 < 6;
             write_iter_45863++) {
            int32_t write_iv_45867 =
                    *(int32_t *) &mem_47644.mem[write_iter_45863 * 4];
            int32_t write_iv_45868 =
                    *(int32_t *) &mem_47641.mem[write_iter_45863 * 4];
            int32_t new_index_46497 = squot32(write_iter_45863, 2);
            int32_t binop_y_46499 = 2 * new_index_46497;
            int32_t new_index_46500 = write_iter_45863 - binop_y_46499;
            bool is_this_one_44333 = write_iv_45867 == 0;
            int32_t this_offset_44334 = -1 + write_iv_45868;
            int32_t total_res_44335;
            
            if (is_this_one_44333) {
                total_res_44335 = this_offset_44334;
            } else {
                total_res_44335 = -1;
            }
            
            bool less_than_zzero_45872 = slt32(total_res_44335, 0);
            bool greater_than_sizze_45873 = sle32(last_offset_44321,
                                                  total_res_44335);
            bool outside_bounds_dim_45874 = less_than_zzero_45872 ||
                 greater_than_sizze_45873;
            
            if (!outside_bounds_dim_45874) {
                memmove(mem_47655.mem + total_res_44335 * 4, mem_47631.mem +
                        (2 * new_index_46497 + new_index_46500) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_45874) {
                struct memblock mem_47670;
                
                mem_47670.references = NULL;
                if (memblock_alloc(ctx, &mem_47670, 4, "mem_47670"))
                    return 1;
                
                int32_t x_48380;
                
                for (int32_t i_48379 = 0; i_48379 < 1; i_48379++) {
                    x_48380 = new_index_46500 + sext_i32_i32(i_48379);
                    *(int32_t *) &mem_47670.mem[i_48379 * 4] = x_48380;
                }
                memmove(mem_47658.mem + total_res_44335 * 4, mem_47670.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47670, "mem_47670") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47670, "mem_47670") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_45874) {
                struct memblock mem_47673;
                
                mem_47673.references = NULL;
                if (memblock_alloc(ctx, &mem_47673, 4, "mem_47673"))
                    return 1;
                
                int32_t x_48382;
                
                for (int32_t i_48381 = 0; i_48381 < 1; i_48381++) {
                    x_48382 = write_iter_45863 + sext_i32_i32(i_48381);
                    *(int32_t *) &mem_47673.mem[i_48381 * 4] = x_48382;
                }
                memmove(mem_47661.mem + total_res_44335 * 4, mem_47673.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47673, "mem_47673") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47673, "mem_47673") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
            return 1;
        
        int32_t conc_tmp_44345 = last_offset_44321 + last_offset_44321;
        int64_t binop_x_47681 = sext_i32_i64(conc_tmp_44345);
        int64_t bytes_47680 = 4 * binop_x_47681;
        struct memblock mem_47682;
        
        mem_47682.references = NULL;
        if (memblock_alloc(ctx, &mem_47682, bytes_47680, "mem_47682"))
            return 1;
        
        int32_t tmp_offs_48383 = 0;
        
        memmove(mem_47682.mem + tmp_offs_48383 * 4, mem_47655.mem + 0,
                last_offset_44321 * sizeof(int32_t));
        tmp_offs_48383 += last_offset_44321;
        memmove(mem_47682.mem + tmp_offs_48383 * 4, mem_47655.mem + 0,
                last_offset_44321 * sizeof(int32_t));
        tmp_offs_48383 += last_offset_44321;
        
        struct memblock mem_47685;
        
        mem_47685.references = NULL;
        if (memblock_alloc(ctx, &mem_47685, bytes_47680, "mem_47685"))
            return 1;
        
        int32_t tmp_offs_48384 = 0;
        
        memmove(mem_47685.mem + tmp_offs_48384 * 4, mem_47658.mem + 0,
                last_offset_44321 * sizeof(int32_t));
        tmp_offs_48384 += last_offset_44321;
        memmove(mem_47685.mem + tmp_offs_48384 * 4, mem_47658.mem + 0,
                last_offset_44321 * sizeof(int32_t));
        tmp_offs_48384 += last_offset_44321;
        
        struct memblock mem_47688;
        
        mem_47688.references = NULL;
        if (memblock_alloc(ctx, &mem_47688, bytes_47680, "mem_47688"))
            return 1;
        
        int32_t tmp_offs_48385 = 0;
        
        memmove(mem_47688.mem + tmp_offs_48385 * 4, mem_47661.mem + 0,
                last_offset_44321 * sizeof(int32_t));
        tmp_offs_48385 += last_offset_44321;
        memmove(mem_47688.mem + tmp_offs_48385 * 4, mem_47661.mem + 0,
                last_offset_44321 * sizeof(int32_t));
        tmp_offs_48385 += last_offset_44321;
        
        bool cond_44349 = conc_tmp_44345 == 0;
        bool loop_cond_44350 = slt32(1, conc_tmp_44345);
        int32_t sizze_44351;
        int32_t sizze_44352;
        int32_t sizze_44353;
        int64_t res_mem_sizze_47731;
        struct memblock res_mem_47732;
        
        res_mem_47732.references = NULL;
        
        int64_t res_mem_sizze_47733;
        struct memblock res_mem_47734;
        
        res_mem_47734.references = NULL;
        
        int64_t res_mem_sizze_47735;
        struct memblock res_mem_47736;
        
        res_mem_47736.references = NULL;
        
        int32_t res_44357;
        
        if (cond_44349) {
            struct memblock mem_47691;
            
            mem_47691.references = NULL;
            if (memblock_alloc(ctx, &mem_47691, bytes_47680, "mem_47691"))
                return 1;
            memmove(mem_47691.mem + 0, mem_47682.mem + 0, conc_tmp_44345 *
                    sizeof(int32_t));
            
            struct memblock mem_47694;
            
            mem_47694.references = NULL;
            if (memblock_alloc(ctx, &mem_47694, bytes_47680, "mem_47694"))
                return 1;
            memmove(mem_47694.mem + 0, mem_47685.mem + 0, conc_tmp_44345 *
                    sizeof(int32_t));
            
            struct memblock mem_47697;
            
            mem_47697.references = NULL;
            if (memblock_alloc(ctx, &mem_47697, bytes_47680, "mem_47697"))
                return 1;
            memmove(mem_47697.mem + 0, mem_47688.mem + 0, conc_tmp_44345 *
                    sizeof(int32_t));
            sizze_44351 = conc_tmp_44345;
            sizze_44352 = conc_tmp_44345;
            sizze_44353 = conc_tmp_44345;
            res_mem_sizze_47731 = bytes_47680;
            if (memblock_set(ctx, &res_mem_47732, &mem_47691, "mem_47691") != 0)
                return 1;
            res_mem_sizze_47733 = bytes_47680;
            if (memblock_set(ctx, &res_mem_47734, &mem_47694, "mem_47694") != 0)
                return 1;
            res_mem_sizze_47735 = bytes_47680;
            if (memblock_set(ctx, &res_mem_47736, &mem_47697, "mem_47697") != 0)
                return 1;
            res_44357 = 0;
            if (memblock_unref(ctx, &mem_47697, "mem_47697") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47694, "mem_47694") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47691, "mem_47691") != 0)
                return 1;
        } else {
            bool res_44369;
            int32_t res_44370;
            int32_t res_44371;
            bool loop_while_44372;
            int32_t r_44373;
            int32_t n_44374;
            
            loop_while_44372 = loop_cond_44350;
            r_44373 = 0;
            n_44374 = conc_tmp_44345;
            while (loop_while_44372) {
                int32_t res_44375 = sdiv32(n_44374, 2);
                int32_t res_44376 = 1 + r_44373;
                bool loop_cond_44377 = slt32(1, res_44375);
                bool loop_while_tmp_48386 = loop_cond_44377;
                int32_t r_tmp_48387 = res_44376;
                int32_t n_tmp_48388;
                
                n_tmp_48388 = res_44375;
                loop_while_44372 = loop_while_tmp_48386;
                r_44373 = r_tmp_48387;
                n_44374 = n_tmp_48388;
            }
            res_44369 = loop_while_44372;
            res_44370 = r_44373;
            res_44371 = n_44374;
            
            int32_t y_44378 = 1 << res_44370;
            bool cond_44379 = conc_tmp_44345 == y_44378;
            int32_t y_44380 = 1 + res_44370;
            int32_t x_44381 = 1 << y_44380;
            int32_t arg_44382 = x_44381 - conc_tmp_44345;
            bool bounds_invalid_upwards_44383 = slt32(arg_44382, 0);
            int32_t conc_tmp_44384 = conc_tmp_44345 + arg_44382;
            int32_t sizze_44385;
            
            if (cond_44379) {
                sizze_44385 = conc_tmp_44345;
            } else {
                sizze_44385 = conc_tmp_44384;
            }
            
            int32_t res_44386;
            
            if (cond_44379) {
                res_44386 = res_44370;
            } else {
                res_44386 = y_44380;
            }
            
            int64_t binop_x_47717 = sext_i32_i64(conc_tmp_44384);
            int64_t bytes_47716 = 4 * binop_x_47717;
            int64_t res_mem_sizze_47725;
            struct memblock res_mem_47726;
            
            res_mem_47726.references = NULL;
            
            int64_t res_mem_sizze_47727;
            struct memblock res_mem_47728;
            
            res_mem_47728.references = NULL;
            
            int64_t res_mem_sizze_47729;
            struct memblock res_mem_47730;
            
            res_mem_47730.references = NULL;
            if (cond_44379) {
                struct memblock mem_47700;
                
                mem_47700.references = NULL;
                if (memblock_alloc(ctx, &mem_47700, bytes_47680, "mem_47700"))
                    return 1;
                memmove(mem_47700.mem + 0, mem_47682.mem + 0, conc_tmp_44345 *
                        sizeof(int32_t));
                
                struct memblock mem_47703;
                
                mem_47703.references = NULL;
                if (memblock_alloc(ctx, &mem_47703, bytes_47680, "mem_47703"))
                    return 1;
                memmove(mem_47703.mem + 0, mem_47685.mem + 0, conc_tmp_44345 *
                        sizeof(int32_t));
                
                struct memblock mem_47706;
                
                mem_47706.references = NULL;
                if (memblock_alloc(ctx, &mem_47706, bytes_47680, "mem_47706"))
                    return 1;
                memmove(mem_47706.mem + 0, mem_47688.mem + 0, conc_tmp_44345 *
                        sizeof(int32_t));
                res_mem_sizze_47725 = bytes_47680;
                if (memblock_set(ctx, &res_mem_47726, &mem_47700,
                                 "mem_47700") != 0)
                    return 1;
                res_mem_sizze_47727 = bytes_47680;
                if (memblock_set(ctx, &res_mem_47728, &mem_47703,
                                 "mem_47703") != 0)
                    return 1;
                res_mem_sizze_47729 = bytes_47680;
                if (memblock_set(ctx, &res_mem_47730, &mem_47706,
                                 "mem_47706") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47706, "mem_47706") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47703, "mem_47703") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47700, "mem_47700") != 0)
                    return 1;
            } else {
                bool y_44404 = slt32(0, conc_tmp_44345);
                bool index_certs_44405;
                
                if (!y_44404) {
                    ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                           "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                           "Index [", 0,
                                           "] out of bounds for array of shape [",
                                           conc_tmp_44345, "].");
                    if (memblock_unref(ctx, &res_mem_47730, "res_mem_47730") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47728, "res_mem_47728") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47726, "res_mem_47726") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47379,
                                       "indexed_mem_47379") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47377,
                                       "indexed_mem_47377") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47375,
                                       "indexed_mem_47375") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                        return 1;
                    return 1;
                }
                
                bool index_concat_cmp_44406 = sle32(last_offset_44321, 0);
                int32_t index_concat_branch_44407;
                
                if (index_concat_cmp_44406) {
                    int32_t index_concat_i_44408 = 0 - last_offset_44321;
                    int32_t index_concat_44409 =
                            *(int32_t *) &mem_47655.mem[index_concat_i_44408 *
                                                        4];
                    
                    index_concat_branch_44407 = index_concat_44409;
                } else {
                    int32_t index_concat_44410 = *(int32_t *) &mem_47655.mem[0];
                    
                    index_concat_branch_44407 = index_concat_44410;
                }
                
                int32_t index_concat_branch_44411;
                
                if (index_concat_cmp_44406) {
                    int32_t index_concat_i_44412 = 0 - last_offset_44321;
                    int32_t index_concat_44413 =
                            *(int32_t *) &mem_47658.mem[index_concat_i_44412 *
                                                        4];
                    
                    index_concat_branch_44411 = index_concat_44413;
                } else {
                    int32_t index_concat_44414 = *(int32_t *) &mem_47658.mem[0];
                    
                    index_concat_branch_44411 = index_concat_44414;
                }
                
                int32_t index_concat_branch_44415;
                
                if (index_concat_cmp_44406) {
                    int32_t index_concat_i_44416 = 0 - last_offset_44321;
                    int32_t index_concat_44417 =
                            *(int32_t *) &mem_47661.mem[index_concat_i_44416 *
                                                        4];
                    
                    index_concat_branch_44415 = index_concat_44417;
                } else {
                    int32_t index_concat_44418 = *(int32_t *) &mem_47661.mem[0];
                    
                    index_concat_branch_44415 = index_concat_44418;
                }
                
                int32_t res_44419;
                int32_t res_44420;
                int32_t res_44421;
                int32_t redout_45890;
                int32_t redout_45891;
                int32_t redout_45892;
                
                redout_45890 = index_concat_branch_44407;
                redout_45891 = index_concat_branch_44411;
                redout_45892 = index_concat_branch_44415;
                for (int32_t i_45893 = 0; i_45893 < conc_tmp_44345; i_45893++) {
                    bool index_concat_cmp_46527 = sle32(last_offset_44321,
                                                        i_45893);
                    int32_t index_concat_branch_46531;
                    
                    if (index_concat_cmp_46527) {
                        int32_t index_concat_i_46528 = i_45893 -
                                last_offset_44321;
                        int32_t index_concat_46529 =
                                *(int32_t *) &mem_47655.mem[index_concat_i_46528 *
                                                            4];
                        
                        index_concat_branch_46531 = index_concat_46529;
                    } else {
                        int32_t index_concat_46530 =
                                *(int32_t *) &mem_47655.mem[i_45893 * 4];
                        
                        index_concat_branch_46531 = index_concat_46530;
                    }
                    
                    int32_t index_concat_branch_46525;
                    
                    if (index_concat_cmp_46527) {
                        int32_t index_concat_i_46522 = i_45893 -
                                last_offset_44321;
                        int32_t index_concat_46523 =
                                *(int32_t *) &mem_47658.mem[index_concat_i_46522 *
                                                            4];
                        
                        index_concat_branch_46525 = index_concat_46523;
                    } else {
                        int32_t index_concat_46524 =
                                *(int32_t *) &mem_47658.mem[i_45893 * 4];
                        
                        index_concat_branch_46525 = index_concat_46524;
                    }
                    
                    int32_t index_concat_branch_46519;
                    
                    if (index_concat_cmp_46527) {
                        int32_t index_concat_i_46516 = i_45893 -
                                last_offset_44321;
                        int32_t index_concat_46517 =
                                *(int32_t *) &mem_47661.mem[index_concat_i_46516 *
                                                            4];
                        
                        index_concat_branch_46519 = index_concat_46517;
                    } else {
                        int32_t index_concat_46518 =
                                *(int32_t *) &mem_47661.mem[i_45893 * 4];
                        
                        index_concat_branch_46519 = index_concat_46518;
                    }
                    
                    bool cond_44428 = redout_45890 == index_concat_branch_46531;
                    bool res_44429 = sle32(redout_45891,
                                           index_concat_branch_46525);
                    bool res_44430 = sle32(redout_45890,
                                           index_concat_branch_46531);
                    bool x_44431 = cond_44428 && res_44429;
                    bool x_44432 = !cond_44428;
                    bool y_44433 = res_44430 && x_44432;
                    bool res_44434 = x_44431 || y_44433;
                    int32_t res_44435;
                    
                    if (res_44434) {
                        res_44435 = index_concat_branch_46531;
                    } else {
                        res_44435 = redout_45890;
                    }
                    
                    int32_t res_44436;
                    
                    if (res_44434) {
                        res_44436 = index_concat_branch_46525;
                    } else {
                        res_44436 = redout_45891;
                    }
                    
                    int32_t res_44437;
                    
                    if (res_44434) {
                        res_44437 = index_concat_branch_46519;
                    } else {
                        res_44437 = redout_45892;
                    }
                    
                    int32_t redout_tmp_48389 = res_44435;
                    int32_t redout_tmp_48390 = res_44436;
                    int32_t redout_tmp_48391;
                    
                    redout_tmp_48391 = res_44437;
                    redout_45890 = redout_tmp_48389;
                    redout_45891 = redout_tmp_48390;
                    redout_45892 = redout_tmp_48391;
                }
                res_44419 = redout_45890;
                res_44420 = redout_45891;
                res_44421 = redout_45892;
                
                bool eq_x_zz_44441 = 0 == arg_44382;
                bool not_p_44442 = !bounds_invalid_upwards_44383;
                bool p_and_eq_x_y_44443 = eq_x_zz_44441 && not_p_44442;
                bool dim_zzero_44444 = bounds_invalid_upwards_44383 ||
                     p_and_eq_x_y_44443;
                bool both_empty_44445 = eq_x_zz_44441 && dim_zzero_44444;
                bool eq_x_y_44446 = arg_44382 == 0;
                bool p_and_eq_x_y_44447 = bounds_invalid_upwards_44383 &&
                     eq_x_y_44446;
                bool dim_match_44448 = not_p_44442 || p_and_eq_x_y_44447;
                bool empty_or_match_44449 = both_empty_44445 || dim_match_44448;
                bool empty_or_match_cert_44450;
                
                if (!empty_or_match_44449) {
                    ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                           "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                           "Function return value does not match shape of type ",
                                           "*", "[", arg_44382, "]", "t");
                    if (memblock_unref(ctx, &res_mem_47730, "res_mem_47730") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47728, "res_mem_47728") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47726, "res_mem_47726") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47379,
                                       "indexed_mem_47379") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47377,
                                       "indexed_mem_47377") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47375,
                                       "indexed_mem_47375") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                        return 1;
                    return 1;
                }
                
                int64_t binop_x_47708 = sext_i32_i64(arg_44382);
                int64_t bytes_47707 = 4 * binop_x_47708;
                struct memblock mem_47709;
                
                mem_47709.references = NULL;
                if (memblock_alloc(ctx, &mem_47709, bytes_47707, "mem_47709"))
                    return 1;
                for (int32_t i_48392 = 0; i_48392 < arg_44382; i_48392++) {
                    *(int32_t *) &mem_47709.mem[i_48392 * 4] = res_44419;
                }
                
                struct memblock mem_47712;
                
                mem_47712.references = NULL;
                if (memblock_alloc(ctx, &mem_47712, bytes_47707, "mem_47712"))
                    return 1;
                for (int32_t i_48393 = 0; i_48393 < arg_44382; i_48393++) {
                    *(int32_t *) &mem_47712.mem[i_48393 * 4] = res_44420;
                }
                
                struct memblock mem_47715;
                
                mem_47715.references = NULL;
                if (memblock_alloc(ctx, &mem_47715, bytes_47707, "mem_47715"))
                    return 1;
                for (int32_t i_48394 = 0; i_48394 < arg_44382; i_48394++) {
                    *(int32_t *) &mem_47715.mem[i_48394 * 4] = res_44421;
                }
                
                struct memblock mem_47718;
                
                mem_47718.references = NULL;
                if (memblock_alloc(ctx, &mem_47718, bytes_47716, "mem_47718"))
                    return 1;
                
                int32_t tmp_offs_48395 = 0;
                
                memmove(mem_47718.mem + tmp_offs_48395 * 4, mem_47655.mem + 0,
                        last_offset_44321 * sizeof(int32_t));
                tmp_offs_48395 += last_offset_44321;
                memmove(mem_47718.mem + tmp_offs_48395 * 4, mem_47655.mem + 0,
                        last_offset_44321 * sizeof(int32_t));
                tmp_offs_48395 += last_offset_44321;
                memmove(mem_47718.mem + tmp_offs_48395 * 4, mem_47709.mem + 0,
                        arg_44382 * sizeof(int32_t));
                tmp_offs_48395 += arg_44382;
                if (memblock_unref(ctx, &mem_47709, "mem_47709") != 0)
                    return 1;
                
                struct memblock mem_47721;
                
                mem_47721.references = NULL;
                if (memblock_alloc(ctx, &mem_47721, bytes_47716, "mem_47721"))
                    return 1;
                
                int32_t tmp_offs_48396 = 0;
                
                memmove(mem_47721.mem + tmp_offs_48396 * 4, mem_47658.mem + 0,
                        last_offset_44321 * sizeof(int32_t));
                tmp_offs_48396 += last_offset_44321;
                memmove(mem_47721.mem + tmp_offs_48396 * 4, mem_47658.mem + 0,
                        last_offset_44321 * sizeof(int32_t));
                tmp_offs_48396 += last_offset_44321;
                memmove(mem_47721.mem + tmp_offs_48396 * 4, mem_47712.mem + 0,
                        arg_44382 * sizeof(int32_t));
                tmp_offs_48396 += arg_44382;
                if (memblock_unref(ctx, &mem_47712, "mem_47712") != 0)
                    return 1;
                
                struct memblock mem_47724;
                
                mem_47724.references = NULL;
                if (memblock_alloc(ctx, &mem_47724, bytes_47716, "mem_47724"))
                    return 1;
                
                int32_t tmp_offs_48397 = 0;
                
                memmove(mem_47724.mem + tmp_offs_48397 * 4, mem_47661.mem + 0,
                        last_offset_44321 * sizeof(int32_t));
                tmp_offs_48397 += last_offset_44321;
                memmove(mem_47724.mem + tmp_offs_48397 * 4, mem_47661.mem + 0,
                        last_offset_44321 * sizeof(int32_t));
                tmp_offs_48397 += last_offset_44321;
                memmove(mem_47724.mem + tmp_offs_48397 * 4, mem_47715.mem + 0,
                        arg_44382 * sizeof(int32_t));
                tmp_offs_48397 += arg_44382;
                if (memblock_unref(ctx, &mem_47715, "mem_47715") != 0)
                    return 1;
                res_mem_sizze_47725 = bytes_47716;
                if (memblock_set(ctx, &res_mem_47726, &mem_47718,
                                 "mem_47718") != 0)
                    return 1;
                res_mem_sizze_47727 = bytes_47716;
                if (memblock_set(ctx, &res_mem_47728, &mem_47721,
                                 "mem_47721") != 0)
                    return 1;
                res_mem_sizze_47729 = bytes_47716;
                if (memblock_set(ctx, &res_mem_47730, &mem_47724,
                                 "mem_47724") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47724, "mem_47724") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47721, "mem_47721") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47718, "mem_47718") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47715, "mem_47715") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47712, "mem_47712") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47709, "mem_47709") != 0)
                    return 1;
            }
            sizze_44351 = sizze_44385;
            sizze_44352 = sizze_44385;
            sizze_44353 = sizze_44385;
            res_mem_sizze_47731 = res_mem_sizze_47725;
            if (memblock_set(ctx, &res_mem_47732, &res_mem_47726,
                             "res_mem_47726") != 0)
                return 1;
            res_mem_sizze_47733 = res_mem_sizze_47727;
            if (memblock_set(ctx, &res_mem_47734, &res_mem_47728,
                             "res_mem_47728") != 0)
                return 1;
            res_mem_sizze_47735 = res_mem_sizze_47729;
            if (memblock_set(ctx, &res_mem_47736, &res_mem_47730,
                             "res_mem_47730") != 0)
                return 1;
            res_44357 = res_44386;
            if (memblock_unref(ctx, &res_mem_47730, "res_mem_47730") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47728, "res_mem_47728") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47726, "res_mem_47726") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
            return 1;
        
        bool dim_zzero_44460 = 0 == sizze_44352;
        bool dim_zzero_44461 = 0 == sizze_44351;
        bool both_empty_44462 = dim_zzero_44460 && dim_zzero_44461;
        bool dim_match_44463 = sizze_44351 == sizze_44352;
        bool empty_or_match_44464 = both_empty_44462 || dim_match_44463;
        bool empty_or_match_cert_44465;
        
        if (!empty_or_match_44464) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        bool dim_zzero_44467 = 0 == sizze_44353;
        bool both_empty_44468 = dim_zzero_44461 && dim_zzero_44467;
        bool dim_match_44469 = sizze_44351 == sizze_44353;
        bool empty_or_match_44470 = both_empty_44468 || dim_match_44469;
        bool empty_or_match_cert_44471;
        
        if (!empty_or_match_44470) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_47750 = sext_i32_i64(sizze_44351);
        int64_t bytes_47749 = 4 * binop_x_47750;
        int64_t indexed_mem_sizze_47776;
        struct memblock indexed_mem_47777;
        
        indexed_mem_47777.references = NULL;
        
        int64_t indexed_mem_sizze_47778;
        struct memblock indexed_mem_47779;
        
        indexed_mem_47779.references = NULL;
        
        int64_t indexed_mem_sizze_47780;
        struct memblock indexed_mem_47781;
        
        indexed_mem_47781.references = NULL;
        
        int64_t xs_mem_sizze_47737;
        struct memblock xs_mem_47738;
        
        xs_mem_47738.references = NULL;
        
        int64_t xs_mem_sizze_47739;
        struct memblock xs_mem_47740;
        
        xs_mem_47740.references = NULL;
        
        int64_t xs_mem_sizze_47741;
        struct memblock xs_mem_47742;
        
        xs_mem_47742.references = NULL;
        xs_mem_sizze_47737 = res_mem_sizze_47731;
        if (memblock_set(ctx, &xs_mem_47738, &res_mem_47732, "res_mem_47732") !=
            0)
            return 1;
        xs_mem_sizze_47739 = res_mem_sizze_47733;
        if (memblock_set(ctx, &xs_mem_47740, &res_mem_47734, "res_mem_47734") !=
            0)
            return 1;
        xs_mem_sizze_47741 = res_mem_sizze_47735;
        if (memblock_set(ctx, &xs_mem_47742, &res_mem_47736, "res_mem_47736") !=
            0)
            return 1;
        for (int32_t i_44488 = 0; i_44488 < res_44357; i_44488++) {
            int32_t upper_bound_44489 = 1 + i_44488;
            int64_t res_mem_sizze_47770;
            struct memblock res_mem_47771;
            
            res_mem_47771.references = NULL;
            
            int64_t res_mem_sizze_47772;
            struct memblock res_mem_47773;
            
            res_mem_47773.references = NULL;
            
            int64_t res_mem_sizze_47774;
            struct memblock res_mem_47775;
            
            res_mem_47775.references = NULL;
            
            int64_t xs_mem_sizze_47743;
            struct memblock xs_mem_47744;
            
            xs_mem_47744.references = NULL;
            
            int64_t xs_mem_sizze_47745;
            struct memblock xs_mem_47746;
            
            xs_mem_47746.references = NULL;
            
            int64_t xs_mem_sizze_47747;
            struct memblock xs_mem_47748;
            
            xs_mem_47748.references = NULL;
            xs_mem_sizze_47743 = xs_mem_sizze_47737;
            if (memblock_set(ctx, &xs_mem_47744, &xs_mem_47738,
                             "xs_mem_47738") != 0)
                return 1;
            xs_mem_sizze_47745 = xs_mem_sizze_47739;
            if (memblock_set(ctx, &xs_mem_47746, &xs_mem_47740,
                             "xs_mem_47740") != 0)
                return 1;
            xs_mem_sizze_47747 = xs_mem_sizze_47741;
            if (memblock_set(ctx, &xs_mem_47748, &xs_mem_47742,
                             "xs_mem_47742") != 0)
                return 1;
            for (int32_t j_44500 = 0; j_44500 < upper_bound_44489; j_44500++) {
                int32_t y_44501 = i_44488 - j_44500;
                int32_t res_44502 = 1 << y_44501;
                struct memblock mem_47751;
                
                mem_47751.references = NULL;
                if (memblock_alloc(ctx, &mem_47751, bytes_47749, "mem_47751"))
                    return 1;
                
                struct memblock mem_47754;
                
                mem_47754.references = NULL;
                if (memblock_alloc(ctx, &mem_47754, bytes_47749, "mem_47754"))
                    return 1;
                
                struct memblock mem_47757;
                
                mem_47757.references = NULL;
                if (memblock_alloc(ctx, &mem_47757, bytes_47749, "mem_47757"))
                    return 1;
                for (int32_t i_45900 = 0; i_45900 < sizze_44351; i_45900++) {
                    int32_t res_44507 = *(int32_t *) &xs_mem_47744.mem[i_45900 *
                                                                       4];
                    int32_t res_44508 = *(int32_t *) &xs_mem_47746.mem[i_45900 *
                                                                       4];
                    int32_t res_44509 = *(int32_t *) &xs_mem_47748.mem[i_45900 *
                                                                       4];
                    int32_t x_44510 = ashr32(i_45900, i_44488);
                    int32_t x_44511 = 2 & x_44510;
                    bool res_44512 = x_44511 == 0;
                    int32_t x_44513 = res_44502 & i_45900;
                    bool cond_44514 = x_44513 == 0;
                    int32_t res_44515;
                    int32_t res_44516;
                    int32_t res_44517;
                    
                    if (cond_44514) {
                        int32_t i_44518 = res_44502 | i_45900;
                        int32_t res_44519 =
                                *(int32_t *) &xs_mem_47744.mem[i_44518 * 4];
                        int32_t res_44520 =
                                *(int32_t *) &xs_mem_47746.mem[i_44518 * 4];
                        int32_t res_44521 =
                                *(int32_t *) &xs_mem_47748.mem[i_44518 * 4];
                        bool cond_44522 = res_44519 == res_44507;
                        bool res_44523 = sle32(res_44520, res_44508);
                        bool res_44524 = sle32(res_44519, res_44507);
                        bool x_44525 = cond_44522 && res_44523;
                        bool x_44526 = !cond_44522;
                        bool y_44527 = res_44524 && x_44526;
                        bool res_44528 = x_44525 || y_44527;
                        bool cond_44529 = res_44528 == res_44512;
                        int32_t res_44530;
                        
                        if (cond_44529) {
                            res_44530 = res_44519;
                        } else {
                            res_44530 = res_44507;
                        }
                        
                        int32_t res_44531;
                        
                        if (cond_44529) {
                            res_44531 = res_44520;
                        } else {
                            res_44531 = res_44508;
                        }
                        
                        int32_t res_44532;
                        
                        if (cond_44529) {
                            res_44532 = res_44521;
                        } else {
                            res_44532 = res_44509;
                        }
                        res_44515 = res_44530;
                        res_44516 = res_44531;
                        res_44517 = res_44532;
                    } else {
                        int32_t i_44533 = res_44502 ^ i_45900;
                        int32_t res_44534 =
                                *(int32_t *) &xs_mem_47744.mem[i_44533 * 4];
                        int32_t res_44535 =
                                *(int32_t *) &xs_mem_47746.mem[i_44533 * 4];
                        int32_t res_44536 =
                                *(int32_t *) &xs_mem_47748.mem[i_44533 * 4];
                        bool cond_44537 = res_44507 == res_44534;
                        bool res_44538 = sle32(res_44508, res_44535);
                        bool res_44539 = sle32(res_44507, res_44534);
                        bool x_44540 = cond_44537 && res_44538;
                        bool x_44541 = !cond_44537;
                        bool y_44542 = res_44539 && x_44541;
                        bool res_44543 = x_44540 || y_44542;
                        bool cond_44544 = res_44543 == res_44512;
                        int32_t res_44545;
                        
                        if (cond_44544) {
                            res_44545 = res_44534;
                        } else {
                            res_44545 = res_44507;
                        }
                        
                        int32_t res_44546;
                        
                        if (cond_44544) {
                            res_44546 = res_44535;
                        } else {
                            res_44546 = res_44508;
                        }
                        
                        int32_t res_44547;
                        
                        if (cond_44544) {
                            res_44547 = res_44536;
                        } else {
                            res_44547 = res_44509;
                        }
                        res_44515 = res_44545;
                        res_44516 = res_44546;
                        res_44517 = res_44547;
                    }
                    *(int32_t *) &mem_47751.mem[i_45900 * 4] = res_44515;
                    *(int32_t *) &mem_47754.mem[i_45900 * 4] = res_44516;
                    *(int32_t *) &mem_47757.mem[i_45900 * 4] = res_44517;
                }
                
                int64_t xs_mem_sizze_tmp_48407 = bytes_47749;
                struct memblock xs_mem_tmp_48408;
                
                xs_mem_tmp_48408.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48408, &mem_47751,
                                 "mem_47751") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_48409 = bytes_47749;
                struct memblock xs_mem_tmp_48410;
                
                xs_mem_tmp_48410.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48410, &mem_47754,
                                 "mem_47754") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_48411 = bytes_47749;
                struct memblock xs_mem_tmp_48412;
                
                xs_mem_tmp_48412.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48412, &mem_47757,
                                 "mem_47757") != 0)
                    return 1;
                xs_mem_sizze_47743 = xs_mem_sizze_tmp_48407;
                if (memblock_set(ctx, &xs_mem_47744, &xs_mem_tmp_48408,
                                 "xs_mem_tmp_48408") != 0)
                    return 1;
                xs_mem_sizze_47745 = xs_mem_sizze_tmp_48409;
                if (memblock_set(ctx, &xs_mem_47746, &xs_mem_tmp_48410,
                                 "xs_mem_tmp_48410") != 0)
                    return 1;
                xs_mem_sizze_47747 = xs_mem_sizze_tmp_48411;
                if (memblock_set(ctx, &xs_mem_47748, &xs_mem_tmp_48412,
                                 "xs_mem_tmp_48412") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48412,
                                   "xs_mem_tmp_48412") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48410,
                                   "xs_mem_tmp_48410") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48408,
                                   "xs_mem_tmp_48408") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47757, "mem_47757") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47754, "mem_47754") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47751, "mem_47751") != 0)
                    return 1;
            }
            res_mem_sizze_47770 = xs_mem_sizze_47743;
            if (memblock_set(ctx, &res_mem_47771, &xs_mem_47744,
                             "xs_mem_47744") != 0)
                return 1;
            res_mem_sizze_47772 = xs_mem_sizze_47745;
            if (memblock_set(ctx, &res_mem_47773, &xs_mem_47746,
                             "xs_mem_47746") != 0)
                return 1;
            res_mem_sizze_47774 = xs_mem_sizze_47747;
            if (memblock_set(ctx, &res_mem_47775, &xs_mem_47748,
                             "xs_mem_47748") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48398 = res_mem_sizze_47770;
            struct memblock xs_mem_tmp_48399;
            
            xs_mem_tmp_48399.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48399, &res_mem_47771,
                             "res_mem_47771") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48400 = res_mem_sizze_47772;
            struct memblock xs_mem_tmp_48401;
            
            xs_mem_tmp_48401.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48401, &res_mem_47773,
                             "res_mem_47773") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48402 = res_mem_sizze_47774;
            struct memblock xs_mem_tmp_48403;
            
            xs_mem_tmp_48403.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48403, &res_mem_47775,
                             "res_mem_47775") != 0)
                return 1;
            xs_mem_sizze_47737 = xs_mem_sizze_tmp_48398;
            if (memblock_set(ctx, &xs_mem_47738, &xs_mem_tmp_48399,
                             "xs_mem_tmp_48399") != 0)
                return 1;
            xs_mem_sizze_47739 = xs_mem_sizze_tmp_48400;
            if (memblock_set(ctx, &xs_mem_47740, &xs_mem_tmp_48401,
                             "xs_mem_tmp_48401") != 0)
                return 1;
            xs_mem_sizze_47741 = xs_mem_sizze_tmp_48402;
            if (memblock_set(ctx, &xs_mem_47742, &xs_mem_tmp_48403,
                             "xs_mem_tmp_48403") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48403, "xs_mem_tmp_48403") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48401, "xs_mem_tmp_48401") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48399, "xs_mem_tmp_48399") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47748, "xs_mem_47748") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47746, "xs_mem_47746") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47744, "xs_mem_47744") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47775, "res_mem_47775") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47773, "res_mem_47773") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47771, "res_mem_47771") != 0)
                return 1;
        }
        indexed_mem_sizze_47776 = xs_mem_sizze_47737;
        if (memblock_set(ctx, &indexed_mem_47777, &xs_mem_47738,
                         "xs_mem_47738") != 0)
            return 1;
        indexed_mem_sizze_47778 = xs_mem_sizze_47739;
        if (memblock_set(ctx, &indexed_mem_47779, &xs_mem_47740,
                         "xs_mem_47740") != 0)
            return 1;
        indexed_mem_sizze_47780 = xs_mem_sizze_47741;
        if (memblock_set(ctx, &indexed_mem_47781, &xs_mem_47742,
                         "xs_mem_47742") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
            return 1;
        
        int32_t m_44548 = conc_tmp_44345 - 1;
        bool zzero_leq_i_p_m_t_s_44549 = sle32(0, m_44548);
        bool i_p_m_t_s_leq_w_44550 = slt32(m_44548, sizze_44351);
        bool y_44552 = zzero_leq_i_p_m_t_s_44549 && i_p_m_t_s_leq_w_44550;
        bool ok_or_empty_44554 = cond_44349 || y_44552;
        bool index_certs_44555;
        
        if (!ok_or_empty_44554) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                                   "Index [", "", ":", conc_tmp_44345,
                                   "] out of bounds for array of shape [",
                                   sizze_44351, "].");
            if (memblock_unref(ctx, &xs_mem_47742, "xs_mem_47742") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47740, "xs_mem_47740") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47738, "xs_mem_47738") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47781, "indexed_mem_47781") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47779, "indexed_mem_47779") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47777, "indexed_mem_47777") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        struct memblock mem_47784;
        
        mem_47784.references = NULL;
        if (memblock_alloc(ctx, &mem_47784, bytes_47680, "mem_47784"))
            return 1;
        
        struct memblock mem_47786;
        
        mem_47786.references = NULL;
        if (memblock_alloc(ctx, &mem_47786, binop_x_47681, "mem_47786"))
            return 1;
        
        int32_t discard_45927;
        int32_t scanacc_45912 = 1;
        
        for (int32_t i_45918 = 0; i_45918 < conc_tmp_44345; i_45918++) {
            int32_t x_44577 = *(int32_t *) &indexed_mem_47777.mem[i_45918 * 4];
            int32_t x_44578 = *(int32_t *) &indexed_mem_47779.mem[i_45918 * 4];
            int32_t i_p_o_46545 = -1 + i_45918;
            int32_t rot_i_46546 = smod32(i_p_o_46545, conc_tmp_44345);
            int32_t x_44579 = *(int32_t *) &indexed_mem_47777.mem[rot_i_46546 *
                                                                  4];
            int32_t x_44580 = *(int32_t *) &indexed_mem_47779.mem[rot_i_46546 *
                                                                  4];
            int32_t x_44581 = *(int32_t *) &indexed_mem_47781.mem[i_45918 * 4];
            bool res_44582 = x_44577 == x_44579;
            bool res_44583 = x_44578 == x_44580;
            bool eq_44584 = res_44582 && res_44583;
            bool res_44585 = !eq_44584;
            int32_t res_44575;
            
            if (res_44585) {
                res_44575 = x_44581;
            } else {
                int32_t res_44576 = x_44581 * scanacc_45912;
                
                res_44575 = res_44576;
            }
            *(int32_t *) &mem_47784.mem[i_45918 * 4] = res_44575;
            *(bool *) &mem_47786.mem[i_45918] = res_44585;
            
            int32_t scanacc_tmp_48419 = res_44575;
            
            scanacc_45912 = scanacc_tmp_48419;
        }
        discard_45927 = scanacc_45912;
        if (memblock_unref(ctx, &indexed_mem_47777, "indexed_mem_47777") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47779, "indexed_mem_47779") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47781, "indexed_mem_47781") != 0)
            return 1;
        
        struct memblock mem_47797;
        
        mem_47797.references = NULL;
        if (memblock_alloc(ctx, &mem_47797, bytes_47680, "mem_47797"))
            return 1;
        
        int32_t discard_45933;
        int32_t scanacc_45929 = 0;
        
        for (int32_t i_45931 = 0; i_45931 < conc_tmp_44345; i_45931++) {
            int32_t i_p_o_46553 = 1 + i_45931;
            int32_t rot_i_46554 = smod32(i_p_o_46553, conc_tmp_44345);
            bool x_44591 = *(bool *) &mem_47786.mem[rot_i_46554];
            int32_t res_44592 = btoi_bool_i32(x_44591);
            int32_t res_44590 = res_44592 + scanacc_45929;
            
            *(int32_t *) &mem_47797.mem[i_45931 * 4] = res_44590;
            
            int32_t scanacc_tmp_48422 = res_44590;
            
            scanacc_45929 = scanacc_tmp_48422;
        }
        discard_45933 = scanacc_45929;
        
        int32_t res_44593;
        
        if (loop_cond_44350) {
            bool index_certs_44596;
            
            if (!zzero_leq_i_p_m_t_s_44549) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                       "Index [", m_44548,
                                       "] out of bounds for array of shape [",
                                       conc_tmp_44345, "].");
                if (memblock_unref(ctx, &mem_47797, "mem_47797") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47786, "mem_47786") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47784, "mem_47784") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47742, "xs_mem_47742") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47740, "xs_mem_47740") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47738, "xs_mem_47738") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47781,
                                   "indexed_mem_47781") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47779,
                                   "indexed_mem_47779") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47777,
                                   "indexed_mem_47777") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47379,
                                   "indexed_mem_47379") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47377,
                                   "indexed_mem_47377") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47375,
                                   "indexed_mem_47375") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                    return 1;
                return 1;
            }
            
            int32_t res_44597 = *(int32_t *) &mem_47797.mem[m_44548 * 4];
            
            res_44593 = res_44597;
        } else {
            res_44593 = 0;
        }
        
        bool bounds_invalid_upwards_44598 = slt32(res_44593, 0);
        bool eq_x_zz_44599 = 0 == res_44593;
        bool not_p_44600 = !bounds_invalid_upwards_44598;
        bool p_and_eq_x_y_44601 = eq_x_zz_44599 && not_p_44600;
        bool dim_zzero_44602 = bounds_invalid_upwards_44598 ||
             p_and_eq_x_y_44601;
        bool both_empty_44603 = eq_x_zz_44599 && dim_zzero_44602;
        bool eq_x_y_44604 = res_44593 == 0;
        bool empty_or_match_44607 = not_p_44600 || both_empty_44603;
        bool empty_or_match_cert_44608;
        
        if (!empty_or_match_44607) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                                   "Function return value does not match shape of type ",
                                   "*", "[", res_44593, "]", "t");
            if (memblock_unref(ctx, &mem_47797, "mem_47797") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47786, "mem_47786") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47784, "mem_47784") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47742, "xs_mem_47742") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47740, "xs_mem_47740") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47738, "xs_mem_47738") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47781, "indexed_mem_47781") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47779, "indexed_mem_47779") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47777, "indexed_mem_47777") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_47803 = sext_i32_i64(res_44593);
        int64_t bytes_47802 = 4 * binop_x_47803;
        struct memblock mem_47804;
        
        mem_47804.references = NULL;
        if (memblock_alloc(ctx, &mem_47804, bytes_47802, "mem_47804"))
            return 1;
        for (int32_t i_48424 = 0; i_48424 < res_44593; i_48424++) {
            *(int32_t *) &mem_47804.mem[i_48424 * 4] = 1;
        }
        for (int32_t write_iter_45934 = 0; write_iter_45934 < conc_tmp_44345;
             write_iter_45934++) {
            int32_t write_iv_45938 =
                    *(int32_t *) &mem_47797.mem[write_iter_45934 * 4];
            int32_t i_p_o_46558 = 1 + write_iter_45934;
            int32_t rot_i_46559 = smod32(i_p_o_46558, conc_tmp_44345);
            bool write_iv_45939 = *(bool *) &mem_47786.mem[rot_i_46559];
            int32_t res_44620;
            
            if (write_iv_45939) {
                int32_t res_44621 = write_iv_45938 - 1;
                
                res_44620 = res_44621;
            } else {
                res_44620 = -1;
            }
            
            bool less_than_zzero_45955 = slt32(res_44620, 0);
            bool greater_than_sizze_45956 = sle32(res_44593, res_44620);
            bool outside_bounds_dim_45957 = less_than_zzero_45955 ||
                 greater_than_sizze_45956;
            
            if (!outside_bounds_dim_45957) {
                memmove(mem_47804.mem + res_44620 * 4, mem_47784.mem +
                        write_iter_45934 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47784, "mem_47784") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47786, "mem_47786") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47797, "mem_47797") != 0)
            return 1;
        
        struct memblock mem_47811;
        
        mem_47811.references = NULL;
        if (memblock_alloc(ctx, &mem_47811, bytes_47802, "mem_47811"))
            return 1;
        
        struct memblock mem_47814;
        
        mem_47814.references = NULL;
        if (memblock_alloc(ctx, &mem_47814, bytes_47802, "mem_47814"))
            return 1;
        
        int32_t discard_45969;
        int32_t scanacc_45963 = 0;
        
        for (int32_t i_45966 = 0; i_45966 < res_44593; i_45966++) {
            int32_t x_44627 = *(int32_t *) &mem_47804.mem[i_45966 * 4];
            bool not_arg_44628 = x_44627 == 0;
            bool res_44629 = !not_arg_44628;
            int32_t part_res_44630;
            
            if (res_44629) {
                part_res_44630 = 0;
            } else {
                part_res_44630 = 1;
            }
            
            int32_t part_res_44631;
            
            if (res_44629) {
                part_res_44631 = 1;
            } else {
                part_res_44631 = 0;
            }
            
            int32_t zz_44626 = part_res_44631 + scanacc_45963;
            
            *(int32_t *) &mem_47811.mem[i_45966 * 4] = zz_44626;
            *(int32_t *) &mem_47814.mem[i_45966 * 4] = part_res_44630;
            
            int32_t scanacc_tmp_48426 = zz_44626;
            
            scanacc_45963 = scanacc_tmp_48426;
        }
        discard_45969 = scanacc_45963;
        
        int32_t last_index_44632 = res_44593 - 1;
        int32_t partition_sizze_44633;
        
        if (eq_x_y_44604) {
            partition_sizze_44633 = 0;
        } else {
            int32_t last_offset_44634 =
                    *(int32_t *) &mem_47811.mem[last_index_44632 * 4];
            
            partition_sizze_44633 = last_offset_44634;
        }
        
        int64_t binop_x_47824 = sext_i32_i64(partition_sizze_44633);
        int64_t bytes_47823 = 4 * binop_x_47824;
        struct memblock mem_47825;
        
        mem_47825.references = NULL;
        if (memblock_alloc(ctx, &mem_47825, bytes_47823, "mem_47825"))
            return 1;
        for (int32_t write_iter_45970 = 0; write_iter_45970 < res_44593;
             write_iter_45970++) {
            int32_t write_iv_45972 =
                    *(int32_t *) &mem_47814.mem[write_iter_45970 * 4];
            int32_t write_iv_45973 =
                    *(int32_t *) &mem_47811.mem[write_iter_45970 * 4];
            bool is_this_one_44642 = write_iv_45972 == 0;
            int32_t this_offset_44643 = -1 + write_iv_45973;
            int32_t total_res_44644;
            
            if (is_this_one_44642) {
                total_res_44644 = this_offset_44643;
            } else {
                total_res_44644 = -1;
            }
            
            bool less_than_zzero_45977 = slt32(total_res_44644, 0);
            bool greater_than_sizze_45978 = sle32(partition_sizze_44633,
                                                  total_res_44644);
            bool outside_bounds_dim_45979 = less_than_zzero_45977 ||
                 greater_than_sizze_45978;
            
            if (!outside_bounds_dim_45979) {
                memmove(mem_47825.mem + total_res_44644 * 4, mem_47804.mem +
                        write_iter_45970 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47804, "mem_47804") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47811, "mem_47811") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47814, "mem_47814") != 0)
            return 1;
        
        bool res_44655;
        bool redout_45983 = 1;
        
        for (int32_t i_45984 = 0; i_45984 < 5; i_45984++) {
            int32_t x_44660 = 1 + i_45984;
            int32_t x_44661 = pow32(x_44660, 2);
            bool y_44663 = slt32(i_45984, partition_sizze_44633);
            bool index_certs_44665;
            
            if (!y_44663) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:34-39 -> tupleTest.fut:151:26-64 -> /futlib/functional.fut:10:39-41 -> tupleTest.fut:151:47-53",
                                       "Index [", i_45984,
                                       "] out of bounds for array of shape [",
                                       partition_sizze_44633, "].");
                if (memblock_unref(ctx, &mem_47825, "mem_47825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47814, "mem_47814") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47811, "mem_47811") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47804, "mem_47804") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47797, "mem_47797") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47786, "mem_47786") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47784, "mem_47784") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47742, "xs_mem_47742") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47740, "xs_mem_47740") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47738, "xs_mem_47738") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47781,
                                   "indexed_mem_47781") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47779,
                                   "indexed_mem_47779") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47777,
                                   "indexed_mem_47777") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47379,
                                   "indexed_mem_47379") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47377,
                                   "indexed_mem_47377") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47375,
                                   "indexed_mem_47375") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                    return 1;
                return 1;
            }
            
            int32_t y_44666 = *(int32_t *) &mem_47825.mem[i_45984 * 4];
            bool res_44667 = x_44661 == y_44666;
            bool x_44658 = res_44667 && redout_45983;
            bool redout_tmp_48430 = x_44658;
            
            redout_45983 = redout_tmp_48430;
        }
        res_44655 = redout_45983;
        if (memblock_unref(ctx, &mem_47825, "mem_47825") != 0)
            return 1;
        cond_44306 = res_44655;
        if (memblock_unref(ctx, &mem_47825, "mem_47825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47814, "mem_47814") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47811, "mem_47811") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47804, "mem_47804") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47797, "mem_47797") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47786, "mem_47786") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47784, "mem_47784") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47742, "xs_mem_47742") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47740, "xs_mem_47740") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47738, "xs_mem_47738") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47781, "indexed_mem_47781") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47779, "indexed_mem_47779") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_47777, "indexed_mem_47777") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47736, "res_mem_47736") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47734, "res_mem_47734") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47732, "res_mem_47732") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47688, "mem_47688") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47685, "mem_47685") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47682, "mem_47682") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47661, "mem_47661") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47658, "mem_47658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47655, "mem_47655") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47644, "mem_47644") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47641, "mem_47641") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47636, "mem_47636") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47631, "mem_47631") != 0)
            return 1;
    } else {
        cond_44306 = 0;
    }
    
    bool res_44668;
    
    if (cond_44306) {
        struct memblock mem_47832;
        
        mem_47832.references = NULL;
        if (memblock_alloc(ctx, &mem_47832, 16, "mem_47832"))
            return 1;
        
        struct memblock mem_47837;
        
        mem_47837.references = NULL;
        if (memblock_alloc(ctx, &mem_47837, 8, "mem_47837"))
            return 1;
        for (int32_t i_45987 = 0; i_45987 < 2; i_45987++) {
            for (int32_t i_48432 = 0; i_48432 < 2; i_48432++) {
                *(int32_t *) &mem_47837.mem[i_48432 * 4] = i_45987;
            }
            memmove(mem_47832.mem + 2 * i_45987 * 4, mem_47837.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
            return 1;
        
        struct memblock mem_47842;
        
        mem_47842.references = NULL;
        if (memblock_alloc(ctx, &mem_47842, 16, "mem_47842"))
            return 1;
        
        struct memblock mem_47845;
        
        mem_47845.references = NULL;
        if (memblock_alloc(ctx, &mem_47845, 16, "mem_47845"))
            return 1;
        
        struct memblock mem_47848;
        
        mem_47848.references = NULL;
        if (memblock_alloc(ctx, &mem_47848, 16, "mem_47848"))
            return 1;
        
        struct memblock mem_47851;
        
        mem_47851.references = NULL;
        if (memblock_alloc(ctx, &mem_47851, 16, "mem_47851"))
            return 1;
        
        int32_t discard_46003;
        int32_t scanacc_45993 = 0;
        
        for (int32_t i_45998 = 0; i_45998 < 4; i_45998++) {
            int32_t x_44681 = smod32(i_45998, 2);
            bool cond_44682 = x_44681 == 0;
            int32_t res_44683;
            
            if (cond_44682) {
                res_44683 = 0;
            } else {
                res_44683 = 1;
            }
            
            bool cond_44684 = x_44681 == 1;
            int32_t res_44685;
            
            if (cond_44684) {
                res_44685 = 0;
            } else {
                res_44685 = 1;
            }
            
            bool res_44686 = !cond_44682;
            int32_t part_res_44687;
            
            if (res_44686) {
                part_res_44687 = 0;
            } else {
                part_res_44687 = 1;
            }
            
            int32_t part_res_44688;
            
            if (res_44686) {
                part_res_44688 = 1;
            } else {
                part_res_44688 = 0;
            }
            
            int32_t zz_44679 = part_res_44688 + scanacc_45993;
            
            *(int32_t *) &mem_47842.mem[i_45998 * 4] = zz_44679;
            *(int32_t *) &mem_47845.mem[i_45998 * 4] = part_res_44687;
            *(int32_t *) &mem_47848.mem[i_45998 * 4] = res_44685;
            *(int32_t *) &mem_47851.mem[i_45998 * 4] = res_44683;
            
            int32_t scanacc_tmp_48433 = zz_44679;
            
            scanacc_45993 = scanacc_tmp_48433;
        }
        discard_46003 = scanacc_45993;
        
        int32_t last_offset_44689 = *(int32_t *) &mem_47842.mem[12];
        int64_t binop_x_47869 = sext_i32_i64(last_offset_44689);
        int64_t bytes_47868 = 4 * binop_x_47869;
        struct memblock mem_47870;
        
        mem_47870.references = NULL;
        if (memblock_alloc(ctx, &mem_47870, bytes_47868, "mem_47870"))
            return 1;
        
        struct memblock mem_47873;
        
        mem_47873.references = NULL;
        if (memblock_alloc(ctx, &mem_47873, bytes_47868, "mem_47873"))
            return 1;
        
        struct memblock mem_47876;
        
        mem_47876.references = NULL;
        if (memblock_alloc(ctx, &mem_47876, bytes_47868, "mem_47876"))
            return 1;
        for (int32_t write_iter_46004 = 0; write_iter_46004 < 4;
             write_iter_46004++) {
            int32_t write_iv_46008 =
                    *(int32_t *) &mem_47845.mem[write_iter_46004 * 4];
            int32_t write_iv_46009 =
                    *(int32_t *) &mem_47842.mem[write_iter_46004 * 4];
            int32_t new_index_46564 = squot32(write_iter_46004, 2);
            int32_t binop_y_46566 = 2 * new_index_46564;
            int32_t new_index_46567 = write_iter_46004 - binop_y_46566;
            bool is_this_one_44701 = write_iv_46008 == 0;
            int32_t this_offset_44702 = -1 + write_iv_46009;
            int32_t total_res_44703;
            
            if (is_this_one_44701) {
                total_res_44703 = this_offset_44702;
            } else {
                total_res_44703 = -1;
            }
            
            bool less_than_zzero_46013 = slt32(total_res_44703, 0);
            bool greater_than_sizze_46014 = sle32(last_offset_44689,
                                                  total_res_44703);
            bool outside_bounds_dim_46015 = less_than_zzero_46013 ||
                 greater_than_sizze_46014;
            
            if (!outside_bounds_dim_46015) {
                memmove(mem_47870.mem + total_res_44703 * 4, mem_47832.mem +
                        (2 * new_index_46564 + new_index_46567) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_46015) {
                struct memblock mem_47885;
                
                mem_47885.references = NULL;
                if (memblock_alloc(ctx, &mem_47885, 4, "mem_47885"))
                    return 1;
                
                int32_t x_48442;
                
                for (int32_t i_48441 = 0; i_48441 < 1; i_48441++) {
                    x_48442 = new_index_46567 + sext_i32_i32(i_48441);
                    *(int32_t *) &mem_47885.mem[i_48441 * 4] = x_48442;
                }
                memmove(mem_47873.mem + total_res_44703 * 4, mem_47885.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47885, "mem_47885") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47885, "mem_47885") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_46015) {
                memmove(mem_47876.mem + total_res_44703 * 4, mem_47851.mem +
                        write_iter_46004 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
            return 1;
        
        struct memblock mem_47894;
        
        mem_47894.references = NULL;
        if (memblock_alloc(ctx, &mem_47894, 16, "mem_47894"))
            return 1;
        
        struct memblock mem_47899;
        
        mem_47899.references = NULL;
        if (memblock_alloc(ctx, &mem_47899, 8, "mem_47899"))
            return 1;
        for (int32_t i_46033 = 0; i_46033 < 2; i_46033++) {
            for (int32_t i_48444 = 0; i_48444 < 2; i_48444++) {
                *(int32_t *) &mem_47899.mem[i_48444 * 4] = i_46033;
            }
            memmove(mem_47894.mem + 2 * i_46033 * 4, mem_47899.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
            return 1;
        
        struct memblock mem_47904;
        
        mem_47904.references = NULL;
        if (memblock_alloc(ctx, &mem_47904, 16, "mem_47904"))
            return 1;
        
        struct memblock mem_47907;
        
        mem_47907.references = NULL;
        if (memblock_alloc(ctx, &mem_47907, 16, "mem_47907"))
            return 1;
        
        int32_t discard_46043;
        int32_t scanacc_46037 = 0;
        
        for (int32_t i_46040 = 0; i_46040 < 4; i_46040++) {
            int32_t x_44722 = *(int32_t *) &mem_47848.mem[i_46040 * 4];
            bool not_arg_44723 = x_44722 == 0;
            bool res_44724 = !not_arg_44723;
            int32_t part_res_44725;
            
            if (res_44724) {
                part_res_44725 = 0;
            } else {
                part_res_44725 = 1;
            }
            
            int32_t part_res_44726;
            
            if (res_44724) {
                part_res_44726 = 1;
            } else {
                part_res_44726 = 0;
            }
            
            int32_t zz_44721 = part_res_44726 + scanacc_46037;
            
            *(int32_t *) &mem_47904.mem[i_46040 * 4] = zz_44721;
            *(int32_t *) &mem_47907.mem[i_46040 * 4] = part_res_44725;
            
            int32_t scanacc_tmp_48445 = zz_44721;
            
            scanacc_46037 = scanacc_tmp_48445;
        }
        discard_46043 = scanacc_46037;
        
        int32_t last_offset_44727 = *(int32_t *) &mem_47904.mem[12];
        int64_t binop_x_47917 = sext_i32_i64(last_offset_44727);
        int64_t bytes_47916 = 4 * binop_x_47917;
        struct memblock mem_47918;
        
        mem_47918.references = NULL;
        if (memblock_alloc(ctx, &mem_47918, bytes_47916, "mem_47918"))
            return 1;
        
        struct memblock mem_47921;
        
        mem_47921.references = NULL;
        if (memblock_alloc(ctx, &mem_47921, bytes_47916, "mem_47921"))
            return 1;
        
        struct memblock mem_47924;
        
        mem_47924.references = NULL;
        if (memblock_alloc(ctx, &mem_47924, bytes_47916, "mem_47924"))
            return 1;
        for (int32_t write_iter_46044 = 0; write_iter_46044 < 4;
             write_iter_46044++) {
            int32_t write_iv_46048 =
                    *(int32_t *) &mem_47907.mem[write_iter_46044 * 4];
            int32_t write_iv_46049 =
                    *(int32_t *) &mem_47904.mem[write_iter_46044 * 4];
            int32_t new_index_46579 = squot32(write_iter_46044, 2);
            int32_t binop_y_46581 = 2 * new_index_46579;
            int32_t new_index_46582 = write_iter_46044 - binop_y_46581;
            bool is_this_one_44739 = write_iv_46048 == 0;
            int32_t this_offset_44740 = -1 + write_iv_46049;
            int32_t total_res_44741;
            
            if (is_this_one_44739) {
                total_res_44741 = this_offset_44740;
            } else {
                total_res_44741 = -1;
            }
            
            bool less_than_zzero_46053 = slt32(total_res_44741, 0);
            bool greater_than_sizze_46054 = sle32(last_offset_44727,
                                                  total_res_44741);
            bool outside_bounds_dim_46055 = less_than_zzero_46053 ||
                 greater_than_sizze_46054;
            
            if (!outside_bounds_dim_46055) {
                memmove(mem_47918.mem + total_res_44741 * 4, mem_47894.mem +
                        (2 * new_index_46579 + new_index_46582) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_46055) {
                struct memblock mem_47933;
                
                mem_47933.references = NULL;
                if (memblock_alloc(ctx, &mem_47933, 4, "mem_47933"))
                    return 1;
                
                int32_t x_48452;
                
                for (int32_t i_48451 = 0; i_48451 < 1; i_48451++) {
                    x_48452 = new_index_46582 + sext_i32_i32(i_48451);
                    *(int32_t *) &mem_47933.mem[i_48451 * 4] = x_48452;
                }
                memmove(mem_47921.mem + total_res_44741 * 4, mem_47933.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_47933, "mem_47933") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47933, "mem_47933") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_46055) {
                memmove(mem_47924.mem + total_res_44741 * 4, mem_47848.mem +
                        write_iter_46044 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
            return 1;
        
        int32_t conc_tmp_44751 = last_offset_44689 + last_offset_44727;
        int64_t binop_x_47941 = sext_i32_i64(conc_tmp_44751);
        int64_t bytes_47940 = 4 * binop_x_47941;
        struct memblock mem_47942;
        
        mem_47942.references = NULL;
        if (memblock_alloc(ctx, &mem_47942, bytes_47940, "mem_47942"))
            return 1;
        
        int32_t tmp_offs_48453 = 0;
        
        memmove(mem_47942.mem + tmp_offs_48453 * 4, mem_47870.mem + 0,
                last_offset_44689 * sizeof(int32_t));
        tmp_offs_48453 += last_offset_44689;
        memmove(mem_47942.mem + tmp_offs_48453 * 4, mem_47918.mem + 0,
                last_offset_44727 * sizeof(int32_t));
        tmp_offs_48453 += last_offset_44727;
        
        struct memblock mem_47945;
        
        mem_47945.references = NULL;
        if (memblock_alloc(ctx, &mem_47945, bytes_47940, "mem_47945"))
            return 1;
        
        int32_t tmp_offs_48454 = 0;
        
        memmove(mem_47945.mem + tmp_offs_48454 * 4, mem_47873.mem + 0,
                last_offset_44689 * sizeof(int32_t));
        tmp_offs_48454 += last_offset_44689;
        memmove(mem_47945.mem + tmp_offs_48454 * 4, mem_47921.mem + 0,
                last_offset_44727 * sizeof(int32_t));
        tmp_offs_48454 += last_offset_44727;
        
        struct memblock mem_47948;
        
        mem_47948.references = NULL;
        if (memblock_alloc(ctx, &mem_47948, bytes_47940, "mem_47948"))
            return 1;
        
        int32_t tmp_offs_48455 = 0;
        
        memmove(mem_47948.mem + tmp_offs_48455 * 4, mem_47876.mem + 0,
                last_offset_44689 * sizeof(int32_t));
        tmp_offs_48455 += last_offset_44689;
        memmove(mem_47948.mem + tmp_offs_48455 * 4, mem_47924.mem + 0,
                last_offset_44727 * sizeof(int32_t));
        tmp_offs_48455 += last_offset_44727;
        
        bool cond_44755 = conc_tmp_44751 == 0;
        bool loop_cond_44756 = slt32(1, conc_tmp_44751);
        int32_t sizze_44757;
        int32_t sizze_44758;
        int32_t sizze_44759;
        int64_t res_mem_sizze_47991;
        struct memblock res_mem_47992;
        
        res_mem_47992.references = NULL;
        
        int64_t res_mem_sizze_47993;
        struct memblock res_mem_47994;
        
        res_mem_47994.references = NULL;
        
        int64_t res_mem_sizze_47995;
        struct memblock res_mem_47996;
        
        res_mem_47996.references = NULL;
        
        int32_t res_44763;
        
        if (cond_44755) {
            struct memblock mem_47951;
            
            mem_47951.references = NULL;
            if (memblock_alloc(ctx, &mem_47951, bytes_47940, "mem_47951"))
                return 1;
            memmove(mem_47951.mem + 0, mem_47942.mem + 0, conc_tmp_44751 *
                    sizeof(int32_t));
            
            struct memblock mem_47954;
            
            mem_47954.references = NULL;
            if (memblock_alloc(ctx, &mem_47954, bytes_47940, "mem_47954"))
                return 1;
            memmove(mem_47954.mem + 0, mem_47945.mem + 0, conc_tmp_44751 *
                    sizeof(int32_t));
            
            struct memblock mem_47957;
            
            mem_47957.references = NULL;
            if (memblock_alloc(ctx, &mem_47957, bytes_47940, "mem_47957"))
                return 1;
            memmove(mem_47957.mem + 0, mem_47948.mem + 0, conc_tmp_44751 *
                    sizeof(int32_t));
            sizze_44757 = conc_tmp_44751;
            sizze_44758 = conc_tmp_44751;
            sizze_44759 = conc_tmp_44751;
            res_mem_sizze_47991 = bytes_47940;
            if (memblock_set(ctx, &res_mem_47992, &mem_47951, "mem_47951") != 0)
                return 1;
            res_mem_sizze_47993 = bytes_47940;
            if (memblock_set(ctx, &res_mem_47994, &mem_47954, "mem_47954") != 0)
                return 1;
            res_mem_sizze_47995 = bytes_47940;
            if (memblock_set(ctx, &res_mem_47996, &mem_47957, "mem_47957") != 0)
                return 1;
            res_44763 = 0;
            if (memblock_unref(ctx, &mem_47957, "mem_47957") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47954, "mem_47954") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47951, "mem_47951") != 0)
                return 1;
        } else {
            bool res_44775;
            int32_t res_44776;
            int32_t res_44777;
            bool loop_while_44778;
            int32_t r_44779;
            int32_t n_44780;
            
            loop_while_44778 = loop_cond_44756;
            r_44779 = 0;
            n_44780 = conc_tmp_44751;
            while (loop_while_44778) {
                int32_t res_44781 = sdiv32(n_44780, 2);
                int32_t res_44782 = 1 + r_44779;
                bool loop_cond_44783 = slt32(1, res_44781);
                bool loop_while_tmp_48456 = loop_cond_44783;
                int32_t r_tmp_48457 = res_44782;
                int32_t n_tmp_48458;
                
                n_tmp_48458 = res_44781;
                loop_while_44778 = loop_while_tmp_48456;
                r_44779 = r_tmp_48457;
                n_44780 = n_tmp_48458;
            }
            res_44775 = loop_while_44778;
            res_44776 = r_44779;
            res_44777 = n_44780;
            
            int32_t y_44784 = 1 << res_44776;
            bool cond_44785 = conc_tmp_44751 == y_44784;
            int32_t y_44786 = 1 + res_44776;
            int32_t x_44787 = 1 << y_44786;
            int32_t arg_44788 = x_44787 - conc_tmp_44751;
            bool bounds_invalid_upwards_44789 = slt32(arg_44788, 0);
            int32_t conc_tmp_44790 = conc_tmp_44751 + arg_44788;
            int32_t sizze_44791;
            
            if (cond_44785) {
                sizze_44791 = conc_tmp_44751;
            } else {
                sizze_44791 = conc_tmp_44790;
            }
            
            int32_t res_44792;
            
            if (cond_44785) {
                res_44792 = res_44776;
            } else {
                res_44792 = y_44786;
            }
            
            int64_t binop_x_47977 = sext_i32_i64(conc_tmp_44790);
            int64_t bytes_47976 = 4 * binop_x_47977;
            int64_t res_mem_sizze_47985;
            struct memblock res_mem_47986;
            
            res_mem_47986.references = NULL;
            
            int64_t res_mem_sizze_47987;
            struct memblock res_mem_47988;
            
            res_mem_47988.references = NULL;
            
            int64_t res_mem_sizze_47989;
            struct memblock res_mem_47990;
            
            res_mem_47990.references = NULL;
            if (cond_44785) {
                struct memblock mem_47960;
                
                mem_47960.references = NULL;
                if (memblock_alloc(ctx, &mem_47960, bytes_47940, "mem_47960"))
                    return 1;
                memmove(mem_47960.mem + 0, mem_47942.mem + 0, conc_tmp_44751 *
                        sizeof(int32_t));
                
                struct memblock mem_47963;
                
                mem_47963.references = NULL;
                if (memblock_alloc(ctx, &mem_47963, bytes_47940, "mem_47963"))
                    return 1;
                memmove(mem_47963.mem + 0, mem_47945.mem + 0, conc_tmp_44751 *
                        sizeof(int32_t));
                
                struct memblock mem_47966;
                
                mem_47966.references = NULL;
                if (memblock_alloc(ctx, &mem_47966, bytes_47940, "mem_47966"))
                    return 1;
                memmove(mem_47966.mem + 0, mem_47948.mem + 0, conc_tmp_44751 *
                        sizeof(int32_t));
                res_mem_sizze_47985 = bytes_47940;
                if (memblock_set(ctx, &res_mem_47986, &mem_47960,
                                 "mem_47960") != 0)
                    return 1;
                res_mem_sizze_47987 = bytes_47940;
                if (memblock_set(ctx, &res_mem_47988, &mem_47963,
                                 "mem_47963") != 0)
                    return 1;
                res_mem_sizze_47989 = bytes_47940;
                if (memblock_set(ctx, &res_mem_47990, &mem_47966,
                                 "mem_47966") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47966, "mem_47966") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47963, "mem_47963") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47960, "mem_47960") != 0)
                    return 1;
            } else {
                bool y_44810 = slt32(0, conc_tmp_44751);
                bool index_certs_44811;
                
                if (!y_44810) {
                    ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                           "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                           "Index [", 0,
                                           "] out of bounds for array of shape [",
                                           conc_tmp_44751, "].");
                    if (memblock_unref(ctx, &res_mem_47990, "res_mem_47990") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47988, "res_mem_47988") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47986, "res_mem_47986") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47379,
                                       "indexed_mem_47379") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47377,
                                       "indexed_mem_47377") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47375,
                                       "indexed_mem_47375") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                        return 1;
                    return 1;
                }
                
                bool index_concat_cmp_44812 = sle32(last_offset_44689, 0);
                int32_t index_concat_branch_44813;
                
                if (index_concat_cmp_44812) {
                    int32_t index_concat_i_44814 = 0 - last_offset_44689;
                    int32_t index_concat_44815 =
                            *(int32_t *) &mem_47918.mem[index_concat_i_44814 *
                                                        4];
                    
                    index_concat_branch_44813 = index_concat_44815;
                } else {
                    int32_t index_concat_44816 = *(int32_t *) &mem_47870.mem[0];
                    
                    index_concat_branch_44813 = index_concat_44816;
                }
                
                int32_t index_concat_branch_44817;
                
                if (index_concat_cmp_44812) {
                    int32_t index_concat_i_44818 = 0 - last_offset_44689;
                    int32_t index_concat_44819 =
                            *(int32_t *) &mem_47921.mem[index_concat_i_44818 *
                                                        4];
                    
                    index_concat_branch_44817 = index_concat_44819;
                } else {
                    int32_t index_concat_44820 = *(int32_t *) &mem_47873.mem[0];
                    
                    index_concat_branch_44817 = index_concat_44820;
                }
                
                int32_t index_concat_branch_44821;
                
                if (index_concat_cmp_44812) {
                    int32_t index_concat_i_44822 = 0 - last_offset_44689;
                    int32_t index_concat_44823 =
                            *(int32_t *) &mem_47924.mem[index_concat_i_44822 *
                                                        4];
                    
                    index_concat_branch_44821 = index_concat_44823;
                } else {
                    int32_t index_concat_44824 = *(int32_t *) &mem_47876.mem[0];
                    
                    index_concat_branch_44821 = index_concat_44824;
                }
                
                int32_t res_44825;
                int32_t res_44826;
                int32_t res_44827;
                int32_t redout_46071;
                int32_t redout_46072;
                int32_t redout_46073;
                
                redout_46071 = index_concat_branch_44813;
                redout_46072 = index_concat_branch_44817;
                redout_46073 = index_concat_branch_44821;
                for (int32_t i_46074 = 0; i_46074 < conc_tmp_44751; i_46074++) {
                    bool index_concat_cmp_46606 = sle32(last_offset_44689,
                                                        i_46074);
                    int32_t index_concat_branch_46610;
                    
                    if (index_concat_cmp_46606) {
                        int32_t index_concat_i_46607 = i_46074 -
                                last_offset_44689;
                        int32_t index_concat_46608 =
                                *(int32_t *) &mem_47918.mem[index_concat_i_46607 *
                                                            4];
                        
                        index_concat_branch_46610 = index_concat_46608;
                    } else {
                        int32_t index_concat_46609 =
                                *(int32_t *) &mem_47870.mem[i_46074 * 4];
                        
                        index_concat_branch_46610 = index_concat_46609;
                    }
                    
                    int32_t index_concat_branch_46604;
                    
                    if (index_concat_cmp_46606) {
                        int32_t index_concat_i_46601 = i_46074 -
                                last_offset_44689;
                        int32_t index_concat_46602 =
                                *(int32_t *) &mem_47921.mem[index_concat_i_46601 *
                                                            4];
                        
                        index_concat_branch_46604 = index_concat_46602;
                    } else {
                        int32_t index_concat_46603 =
                                *(int32_t *) &mem_47873.mem[i_46074 * 4];
                        
                        index_concat_branch_46604 = index_concat_46603;
                    }
                    
                    int32_t index_concat_branch_46598;
                    
                    if (index_concat_cmp_46606) {
                        int32_t index_concat_i_46595 = i_46074 -
                                last_offset_44689;
                        int32_t index_concat_46596 =
                                *(int32_t *) &mem_47924.mem[index_concat_i_46595 *
                                                            4];
                        
                        index_concat_branch_46598 = index_concat_46596;
                    } else {
                        int32_t index_concat_46597 =
                                *(int32_t *) &mem_47876.mem[i_46074 * 4];
                        
                        index_concat_branch_46598 = index_concat_46597;
                    }
                    
                    bool cond_44834 = redout_46071 == index_concat_branch_46610;
                    bool res_44835 = sle32(redout_46072,
                                           index_concat_branch_46604);
                    bool res_44836 = sle32(redout_46071,
                                           index_concat_branch_46610);
                    bool x_44837 = cond_44834 && res_44835;
                    bool x_44838 = !cond_44834;
                    bool y_44839 = res_44836 && x_44838;
                    bool res_44840 = x_44837 || y_44839;
                    int32_t res_44841;
                    
                    if (res_44840) {
                        res_44841 = index_concat_branch_46610;
                    } else {
                        res_44841 = redout_46071;
                    }
                    
                    int32_t res_44842;
                    
                    if (res_44840) {
                        res_44842 = index_concat_branch_46604;
                    } else {
                        res_44842 = redout_46072;
                    }
                    
                    int32_t res_44843;
                    
                    if (res_44840) {
                        res_44843 = index_concat_branch_46598;
                    } else {
                        res_44843 = redout_46073;
                    }
                    
                    int32_t redout_tmp_48459 = res_44841;
                    int32_t redout_tmp_48460 = res_44842;
                    int32_t redout_tmp_48461;
                    
                    redout_tmp_48461 = res_44843;
                    redout_46071 = redout_tmp_48459;
                    redout_46072 = redout_tmp_48460;
                    redout_46073 = redout_tmp_48461;
                }
                res_44825 = redout_46071;
                res_44826 = redout_46072;
                res_44827 = redout_46073;
                
                bool eq_x_zz_44847 = 0 == arg_44788;
                bool not_p_44848 = !bounds_invalid_upwards_44789;
                bool p_and_eq_x_y_44849 = eq_x_zz_44847 && not_p_44848;
                bool dim_zzero_44850 = bounds_invalid_upwards_44789 ||
                     p_and_eq_x_y_44849;
                bool both_empty_44851 = eq_x_zz_44847 && dim_zzero_44850;
                bool eq_x_y_44852 = arg_44788 == 0;
                bool p_and_eq_x_y_44853 = bounds_invalid_upwards_44789 &&
                     eq_x_y_44852;
                bool dim_match_44854 = not_p_44848 || p_and_eq_x_y_44853;
                bool empty_or_match_44855 = both_empty_44851 || dim_match_44854;
                bool empty_or_match_cert_44856;
                
                if (!empty_or_match_44855) {
                    ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                           "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                           "Function return value does not match shape of type ",
                                           "*", "[", arg_44788, "]", "t");
                    if (memblock_unref(ctx, &res_mem_47990, "res_mem_47990") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47988, "res_mem_47988") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47986, "res_mem_47986") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47379,
                                       "indexed_mem_47379") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47377,
                                       "indexed_mem_47377") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_47375,
                                       "indexed_mem_47375") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                        return 1;
                    return 1;
                }
                
                int64_t binop_x_47968 = sext_i32_i64(arg_44788);
                int64_t bytes_47967 = 4 * binop_x_47968;
                struct memblock mem_47969;
                
                mem_47969.references = NULL;
                if (memblock_alloc(ctx, &mem_47969, bytes_47967, "mem_47969"))
                    return 1;
                for (int32_t i_48462 = 0; i_48462 < arg_44788; i_48462++) {
                    *(int32_t *) &mem_47969.mem[i_48462 * 4] = res_44825;
                }
                
                struct memblock mem_47972;
                
                mem_47972.references = NULL;
                if (memblock_alloc(ctx, &mem_47972, bytes_47967, "mem_47972"))
                    return 1;
                for (int32_t i_48463 = 0; i_48463 < arg_44788; i_48463++) {
                    *(int32_t *) &mem_47972.mem[i_48463 * 4] = res_44826;
                }
                
                struct memblock mem_47975;
                
                mem_47975.references = NULL;
                if (memblock_alloc(ctx, &mem_47975, bytes_47967, "mem_47975"))
                    return 1;
                for (int32_t i_48464 = 0; i_48464 < arg_44788; i_48464++) {
                    *(int32_t *) &mem_47975.mem[i_48464 * 4] = res_44827;
                }
                
                struct memblock mem_47978;
                
                mem_47978.references = NULL;
                if (memblock_alloc(ctx, &mem_47978, bytes_47976, "mem_47978"))
                    return 1;
                
                int32_t tmp_offs_48465 = 0;
                
                memmove(mem_47978.mem + tmp_offs_48465 * 4, mem_47870.mem + 0,
                        last_offset_44689 * sizeof(int32_t));
                tmp_offs_48465 += last_offset_44689;
                memmove(mem_47978.mem + tmp_offs_48465 * 4, mem_47918.mem + 0,
                        last_offset_44727 * sizeof(int32_t));
                tmp_offs_48465 += last_offset_44727;
                memmove(mem_47978.mem + tmp_offs_48465 * 4, mem_47969.mem + 0,
                        arg_44788 * sizeof(int32_t));
                tmp_offs_48465 += arg_44788;
                if (memblock_unref(ctx, &mem_47969, "mem_47969") != 0)
                    return 1;
                
                struct memblock mem_47981;
                
                mem_47981.references = NULL;
                if (memblock_alloc(ctx, &mem_47981, bytes_47976, "mem_47981"))
                    return 1;
                
                int32_t tmp_offs_48466 = 0;
                
                memmove(mem_47981.mem + tmp_offs_48466 * 4, mem_47873.mem + 0,
                        last_offset_44689 * sizeof(int32_t));
                tmp_offs_48466 += last_offset_44689;
                memmove(mem_47981.mem + tmp_offs_48466 * 4, mem_47921.mem + 0,
                        last_offset_44727 * sizeof(int32_t));
                tmp_offs_48466 += last_offset_44727;
                memmove(mem_47981.mem + tmp_offs_48466 * 4, mem_47972.mem + 0,
                        arg_44788 * sizeof(int32_t));
                tmp_offs_48466 += arg_44788;
                if (memblock_unref(ctx, &mem_47972, "mem_47972") != 0)
                    return 1;
                
                struct memblock mem_47984;
                
                mem_47984.references = NULL;
                if (memblock_alloc(ctx, &mem_47984, bytes_47976, "mem_47984"))
                    return 1;
                
                int32_t tmp_offs_48467 = 0;
                
                memmove(mem_47984.mem + tmp_offs_48467 * 4, mem_47876.mem + 0,
                        last_offset_44689 * sizeof(int32_t));
                tmp_offs_48467 += last_offset_44689;
                memmove(mem_47984.mem + tmp_offs_48467 * 4, mem_47924.mem + 0,
                        last_offset_44727 * sizeof(int32_t));
                tmp_offs_48467 += last_offset_44727;
                memmove(mem_47984.mem + tmp_offs_48467 * 4, mem_47975.mem + 0,
                        arg_44788 * sizeof(int32_t));
                tmp_offs_48467 += arg_44788;
                if (memblock_unref(ctx, &mem_47975, "mem_47975") != 0)
                    return 1;
                res_mem_sizze_47985 = bytes_47976;
                if (memblock_set(ctx, &res_mem_47986, &mem_47978,
                                 "mem_47978") != 0)
                    return 1;
                res_mem_sizze_47987 = bytes_47976;
                if (memblock_set(ctx, &res_mem_47988, &mem_47981,
                                 "mem_47981") != 0)
                    return 1;
                res_mem_sizze_47989 = bytes_47976;
                if (memblock_set(ctx, &res_mem_47990, &mem_47984,
                                 "mem_47984") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47984, "mem_47984") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47981, "mem_47981") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47978, "mem_47978") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47975, "mem_47975") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47972, "mem_47972") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47969, "mem_47969") != 0)
                    return 1;
            }
            sizze_44757 = sizze_44791;
            sizze_44758 = sizze_44791;
            sizze_44759 = sizze_44791;
            res_mem_sizze_47991 = res_mem_sizze_47985;
            if (memblock_set(ctx, &res_mem_47992, &res_mem_47986,
                             "res_mem_47986") != 0)
                return 1;
            res_mem_sizze_47993 = res_mem_sizze_47987;
            if (memblock_set(ctx, &res_mem_47994, &res_mem_47988,
                             "res_mem_47988") != 0)
                return 1;
            res_mem_sizze_47995 = res_mem_sizze_47989;
            if (memblock_set(ctx, &res_mem_47996, &res_mem_47990,
                             "res_mem_47990") != 0)
                return 1;
            res_44763 = res_44792;
            if (memblock_unref(ctx, &res_mem_47990, "res_mem_47990") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47988, "res_mem_47988") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47986, "res_mem_47986") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
            return 1;
        
        bool dim_zzero_44866 = 0 == sizze_44758;
        bool dim_zzero_44867 = 0 == sizze_44757;
        bool both_empty_44868 = dim_zzero_44866 && dim_zzero_44867;
        bool dim_match_44869 = sizze_44757 == sizze_44758;
        bool empty_or_match_44870 = both_empty_44868 || dim_match_44869;
        bool empty_or_match_cert_44871;
        
        if (!empty_or_match_44870) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        bool dim_zzero_44873 = 0 == sizze_44759;
        bool both_empty_44874 = dim_zzero_44867 && dim_zzero_44873;
        bool dim_match_44875 = sizze_44757 == sizze_44759;
        bool empty_or_match_44876 = both_empty_44874 || dim_match_44875;
        bool empty_or_match_cert_44877;
        
        if (!empty_or_match_44876) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_48010 = sext_i32_i64(sizze_44757);
        int64_t bytes_48009 = 4 * binop_x_48010;
        int64_t indexed_mem_sizze_48036;
        struct memblock indexed_mem_48037;
        
        indexed_mem_48037.references = NULL;
        
        int64_t indexed_mem_sizze_48038;
        struct memblock indexed_mem_48039;
        
        indexed_mem_48039.references = NULL;
        
        int64_t indexed_mem_sizze_48040;
        struct memblock indexed_mem_48041;
        
        indexed_mem_48041.references = NULL;
        
        int64_t xs_mem_sizze_47997;
        struct memblock xs_mem_47998;
        
        xs_mem_47998.references = NULL;
        
        int64_t xs_mem_sizze_47999;
        struct memblock xs_mem_48000;
        
        xs_mem_48000.references = NULL;
        
        int64_t xs_mem_sizze_48001;
        struct memblock xs_mem_48002;
        
        xs_mem_48002.references = NULL;
        xs_mem_sizze_47997 = res_mem_sizze_47991;
        if (memblock_set(ctx, &xs_mem_47998, &res_mem_47992, "res_mem_47992") !=
            0)
            return 1;
        xs_mem_sizze_47999 = res_mem_sizze_47993;
        if (memblock_set(ctx, &xs_mem_48000, &res_mem_47994, "res_mem_47994") !=
            0)
            return 1;
        xs_mem_sizze_48001 = res_mem_sizze_47995;
        if (memblock_set(ctx, &xs_mem_48002, &res_mem_47996, "res_mem_47996") !=
            0)
            return 1;
        for (int32_t i_44894 = 0; i_44894 < res_44763; i_44894++) {
            int32_t upper_bound_44895 = 1 + i_44894;
            int64_t res_mem_sizze_48030;
            struct memblock res_mem_48031;
            
            res_mem_48031.references = NULL;
            
            int64_t res_mem_sizze_48032;
            struct memblock res_mem_48033;
            
            res_mem_48033.references = NULL;
            
            int64_t res_mem_sizze_48034;
            struct memblock res_mem_48035;
            
            res_mem_48035.references = NULL;
            
            int64_t xs_mem_sizze_48003;
            struct memblock xs_mem_48004;
            
            xs_mem_48004.references = NULL;
            
            int64_t xs_mem_sizze_48005;
            struct memblock xs_mem_48006;
            
            xs_mem_48006.references = NULL;
            
            int64_t xs_mem_sizze_48007;
            struct memblock xs_mem_48008;
            
            xs_mem_48008.references = NULL;
            xs_mem_sizze_48003 = xs_mem_sizze_47997;
            if (memblock_set(ctx, &xs_mem_48004, &xs_mem_47998,
                             "xs_mem_47998") != 0)
                return 1;
            xs_mem_sizze_48005 = xs_mem_sizze_47999;
            if (memblock_set(ctx, &xs_mem_48006, &xs_mem_48000,
                             "xs_mem_48000") != 0)
                return 1;
            xs_mem_sizze_48007 = xs_mem_sizze_48001;
            if (memblock_set(ctx, &xs_mem_48008, &xs_mem_48002,
                             "xs_mem_48002") != 0)
                return 1;
            for (int32_t j_44906 = 0; j_44906 < upper_bound_44895; j_44906++) {
                int32_t y_44907 = i_44894 - j_44906;
                int32_t res_44908 = 1 << y_44907;
                struct memblock mem_48011;
                
                mem_48011.references = NULL;
                if (memblock_alloc(ctx, &mem_48011, bytes_48009, "mem_48011"))
                    return 1;
                
                struct memblock mem_48014;
                
                mem_48014.references = NULL;
                if (memblock_alloc(ctx, &mem_48014, bytes_48009, "mem_48014"))
                    return 1;
                
                struct memblock mem_48017;
                
                mem_48017.references = NULL;
                if (memblock_alloc(ctx, &mem_48017, bytes_48009, "mem_48017"))
                    return 1;
                for (int32_t i_46081 = 0; i_46081 < sizze_44757; i_46081++) {
                    int32_t res_44913 = *(int32_t *) &xs_mem_48004.mem[i_46081 *
                                                                       4];
                    int32_t res_44914 = *(int32_t *) &xs_mem_48006.mem[i_46081 *
                                                                       4];
                    int32_t res_44915 = *(int32_t *) &xs_mem_48008.mem[i_46081 *
                                                                       4];
                    int32_t x_44916 = ashr32(i_46081, i_44894);
                    int32_t x_44917 = 2 & x_44916;
                    bool res_44918 = x_44917 == 0;
                    int32_t x_44919 = res_44908 & i_46081;
                    bool cond_44920 = x_44919 == 0;
                    int32_t res_44921;
                    int32_t res_44922;
                    int32_t res_44923;
                    
                    if (cond_44920) {
                        int32_t i_44924 = res_44908 | i_46081;
                        int32_t res_44925 =
                                *(int32_t *) &xs_mem_48004.mem[i_44924 * 4];
                        int32_t res_44926 =
                                *(int32_t *) &xs_mem_48006.mem[i_44924 * 4];
                        int32_t res_44927 =
                                *(int32_t *) &xs_mem_48008.mem[i_44924 * 4];
                        bool cond_44928 = res_44925 == res_44913;
                        bool res_44929 = sle32(res_44926, res_44914);
                        bool res_44930 = sle32(res_44925, res_44913);
                        bool x_44931 = cond_44928 && res_44929;
                        bool x_44932 = !cond_44928;
                        bool y_44933 = res_44930 && x_44932;
                        bool res_44934 = x_44931 || y_44933;
                        bool cond_44935 = res_44934 == res_44918;
                        int32_t res_44936;
                        
                        if (cond_44935) {
                            res_44936 = res_44925;
                        } else {
                            res_44936 = res_44913;
                        }
                        
                        int32_t res_44937;
                        
                        if (cond_44935) {
                            res_44937 = res_44926;
                        } else {
                            res_44937 = res_44914;
                        }
                        
                        int32_t res_44938;
                        
                        if (cond_44935) {
                            res_44938 = res_44927;
                        } else {
                            res_44938 = res_44915;
                        }
                        res_44921 = res_44936;
                        res_44922 = res_44937;
                        res_44923 = res_44938;
                    } else {
                        int32_t i_44939 = res_44908 ^ i_46081;
                        int32_t res_44940 =
                                *(int32_t *) &xs_mem_48004.mem[i_44939 * 4];
                        int32_t res_44941 =
                                *(int32_t *) &xs_mem_48006.mem[i_44939 * 4];
                        int32_t res_44942 =
                                *(int32_t *) &xs_mem_48008.mem[i_44939 * 4];
                        bool cond_44943 = res_44913 == res_44940;
                        bool res_44944 = sle32(res_44914, res_44941);
                        bool res_44945 = sle32(res_44913, res_44940);
                        bool x_44946 = cond_44943 && res_44944;
                        bool x_44947 = !cond_44943;
                        bool y_44948 = res_44945 && x_44947;
                        bool res_44949 = x_44946 || y_44948;
                        bool cond_44950 = res_44949 == res_44918;
                        int32_t res_44951;
                        
                        if (cond_44950) {
                            res_44951 = res_44940;
                        } else {
                            res_44951 = res_44913;
                        }
                        
                        int32_t res_44952;
                        
                        if (cond_44950) {
                            res_44952 = res_44941;
                        } else {
                            res_44952 = res_44914;
                        }
                        
                        int32_t res_44953;
                        
                        if (cond_44950) {
                            res_44953 = res_44942;
                        } else {
                            res_44953 = res_44915;
                        }
                        res_44921 = res_44951;
                        res_44922 = res_44952;
                        res_44923 = res_44953;
                    }
                    *(int32_t *) &mem_48011.mem[i_46081 * 4] = res_44921;
                    *(int32_t *) &mem_48014.mem[i_46081 * 4] = res_44922;
                    *(int32_t *) &mem_48017.mem[i_46081 * 4] = res_44923;
                }
                
                int64_t xs_mem_sizze_tmp_48477 = bytes_48009;
                struct memblock xs_mem_tmp_48478;
                
                xs_mem_tmp_48478.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48478, &mem_48011,
                                 "mem_48011") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_48479 = bytes_48009;
                struct memblock xs_mem_tmp_48480;
                
                xs_mem_tmp_48480.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48480, &mem_48014,
                                 "mem_48014") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_48481 = bytes_48009;
                struct memblock xs_mem_tmp_48482;
                
                xs_mem_tmp_48482.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_48482, &mem_48017,
                                 "mem_48017") != 0)
                    return 1;
                xs_mem_sizze_48003 = xs_mem_sizze_tmp_48477;
                if (memblock_set(ctx, &xs_mem_48004, &xs_mem_tmp_48478,
                                 "xs_mem_tmp_48478") != 0)
                    return 1;
                xs_mem_sizze_48005 = xs_mem_sizze_tmp_48479;
                if (memblock_set(ctx, &xs_mem_48006, &xs_mem_tmp_48480,
                                 "xs_mem_tmp_48480") != 0)
                    return 1;
                xs_mem_sizze_48007 = xs_mem_sizze_tmp_48481;
                if (memblock_set(ctx, &xs_mem_48008, &xs_mem_tmp_48482,
                                 "xs_mem_tmp_48482") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48482,
                                   "xs_mem_tmp_48482") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48480,
                                   "xs_mem_tmp_48480") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_48478,
                                   "xs_mem_tmp_48478") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_48017, "mem_48017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_48014, "mem_48014") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_48011, "mem_48011") != 0)
                    return 1;
            }
            res_mem_sizze_48030 = xs_mem_sizze_48003;
            if (memblock_set(ctx, &res_mem_48031, &xs_mem_48004,
                             "xs_mem_48004") != 0)
                return 1;
            res_mem_sizze_48032 = xs_mem_sizze_48005;
            if (memblock_set(ctx, &res_mem_48033, &xs_mem_48006,
                             "xs_mem_48006") != 0)
                return 1;
            res_mem_sizze_48034 = xs_mem_sizze_48007;
            if (memblock_set(ctx, &res_mem_48035, &xs_mem_48008,
                             "xs_mem_48008") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48468 = res_mem_sizze_48030;
            struct memblock xs_mem_tmp_48469;
            
            xs_mem_tmp_48469.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48469, &res_mem_48031,
                             "res_mem_48031") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48470 = res_mem_sizze_48032;
            struct memblock xs_mem_tmp_48471;
            
            xs_mem_tmp_48471.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48471, &res_mem_48033,
                             "res_mem_48033") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_48472 = res_mem_sizze_48034;
            struct memblock xs_mem_tmp_48473;
            
            xs_mem_tmp_48473.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_48473, &res_mem_48035,
                             "res_mem_48035") != 0)
                return 1;
            xs_mem_sizze_47997 = xs_mem_sizze_tmp_48468;
            if (memblock_set(ctx, &xs_mem_47998, &xs_mem_tmp_48469,
                             "xs_mem_tmp_48469") != 0)
                return 1;
            xs_mem_sizze_47999 = xs_mem_sizze_tmp_48470;
            if (memblock_set(ctx, &xs_mem_48000, &xs_mem_tmp_48471,
                             "xs_mem_tmp_48471") != 0)
                return 1;
            xs_mem_sizze_48001 = xs_mem_sizze_tmp_48472;
            if (memblock_set(ctx, &xs_mem_48002, &xs_mem_tmp_48473,
                             "xs_mem_tmp_48473") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48473, "xs_mem_tmp_48473") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48471, "xs_mem_tmp_48471") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_48469, "xs_mem_tmp_48469") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_48008, "xs_mem_48008") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_48006, "xs_mem_48006") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_48004, "xs_mem_48004") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_48035, "res_mem_48035") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_48033, "res_mem_48033") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_48031, "res_mem_48031") != 0)
                return 1;
        }
        indexed_mem_sizze_48036 = xs_mem_sizze_47997;
        if (memblock_set(ctx, &indexed_mem_48037, &xs_mem_47998,
                         "xs_mem_47998") != 0)
            return 1;
        indexed_mem_sizze_48038 = xs_mem_sizze_47999;
        if (memblock_set(ctx, &indexed_mem_48039, &xs_mem_48000,
                         "xs_mem_48000") != 0)
            return 1;
        indexed_mem_sizze_48040 = xs_mem_sizze_48001;
        if (memblock_set(ctx, &indexed_mem_48041, &xs_mem_48002,
                         "xs_mem_48002") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
            return 1;
        
        int32_t m_44954 = conc_tmp_44751 - 1;
        bool zzero_leq_i_p_m_t_s_44955 = sle32(0, m_44954);
        bool i_p_m_t_s_leq_w_44956 = slt32(m_44954, sizze_44757);
        bool y_44958 = zzero_leq_i_p_m_t_s_44955 && i_p_m_t_s_leq_w_44956;
        bool ok_or_empty_44960 = cond_44755 || y_44958;
        bool index_certs_44961;
        
        if (!ok_or_empty_44960) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                                   "Index [", "", ":", conc_tmp_44751,
                                   "] out of bounds for array of shape [",
                                   sizze_44757, "].");
            if (memblock_unref(ctx, &xs_mem_48002, "xs_mem_48002") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_48000, "xs_mem_48000") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47998, "xs_mem_47998") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_48041, "indexed_mem_48041") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_48039, "indexed_mem_48039") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_48037, "indexed_mem_48037") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        struct memblock mem_48044;
        
        mem_48044.references = NULL;
        if (memblock_alloc(ctx, &mem_48044, bytes_47940, "mem_48044"))
            return 1;
        
        struct memblock mem_48046;
        
        mem_48046.references = NULL;
        if (memblock_alloc(ctx, &mem_48046, binop_x_47941, "mem_48046"))
            return 1;
        
        int32_t discard_46108;
        int32_t scanacc_46093 = 1;
        
        for (int32_t i_46099 = 0; i_46099 < conc_tmp_44751; i_46099++) {
            int32_t x_44983 = *(int32_t *) &indexed_mem_48037.mem[i_46099 * 4];
            int32_t x_44984 = *(int32_t *) &indexed_mem_48039.mem[i_46099 * 4];
            int32_t i_p_o_46624 = -1 + i_46099;
            int32_t rot_i_46625 = smod32(i_p_o_46624, conc_tmp_44751);
            int32_t x_44985 = *(int32_t *) &indexed_mem_48037.mem[rot_i_46625 *
                                                                  4];
            int32_t x_44986 = *(int32_t *) &indexed_mem_48039.mem[rot_i_46625 *
                                                                  4];
            int32_t x_44987 = *(int32_t *) &indexed_mem_48041.mem[i_46099 * 4];
            bool res_44988 = x_44983 == x_44985;
            bool res_44989 = x_44984 == x_44986;
            bool eq_44990 = res_44988 && res_44989;
            bool res_44991 = !eq_44990;
            int32_t res_44981;
            
            if (res_44991) {
                res_44981 = x_44987;
            } else {
                int32_t res_44982 = x_44987 * scanacc_46093;
                
                res_44981 = res_44982;
            }
            *(int32_t *) &mem_48044.mem[i_46099 * 4] = res_44981;
            *(bool *) &mem_48046.mem[i_46099] = res_44991;
            
            int32_t scanacc_tmp_48489 = res_44981;
            
            scanacc_46093 = scanacc_tmp_48489;
        }
        discard_46108 = scanacc_46093;
        if (memblock_unref(ctx, &indexed_mem_48037, "indexed_mem_48037") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_48039, "indexed_mem_48039") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_48041, "indexed_mem_48041") != 0)
            return 1;
        
        struct memblock mem_48057;
        
        mem_48057.references = NULL;
        if (memblock_alloc(ctx, &mem_48057, bytes_47940, "mem_48057"))
            return 1;
        
        int32_t discard_46114;
        int32_t scanacc_46110 = 0;
        
        for (int32_t i_46112 = 0; i_46112 < conc_tmp_44751; i_46112++) {
            int32_t i_p_o_46632 = 1 + i_46112;
            int32_t rot_i_46633 = smod32(i_p_o_46632, conc_tmp_44751);
            bool x_44997 = *(bool *) &mem_48046.mem[rot_i_46633];
            int32_t res_44998 = btoi_bool_i32(x_44997);
            int32_t res_44996 = res_44998 + scanacc_46110;
            
            *(int32_t *) &mem_48057.mem[i_46112 * 4] = res_44996;
            
            int32_t scanacc_tmp_48492 = res_44996;
            
            scanacc_46110 = scanacc_tmp_48492;
        }
        discard_46114 = scanacc_46110;
        
        int32_t res_44999;
        
        if (loop_cond_44756) {
            bool index_certs_45002;
            
            if (!zzero_leq_i_p_m_t_s_44955) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                       "Index [", m_44954,
                                       "] out of bounds for array of shape [",
                                       conc_tmp_44751, "].");
                if (memblock_unref(ctx, &mem_48057, "mem_48057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_48046, "mem_48046") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_48044, "mem_48044") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_48002, "xs_mem_48002") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_48000, "xs_mem_48000") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47998, "xs_mem_47998") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_48041,
                                   "indexed_mem_48041") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_48039,
                                   "indexed_mem_48039") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_48037,
                                   "indexed_mem_48037") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47379,
                                   "indexed_mem_47379") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47377,
                                   "indexed_mem_47377") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_47375,
                                   "indexed_mem_47375") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                    return 1;
                return 1;
            }
            
            int32_t res_45003 = *(int32_t *) &mem_48057.mem[m_44954 * 4];
            
            res_44999 = res_45003;
        } else {
            res_44999 = 0;
        }
        
        bool bounds_invalid_upwards_45004 = slt32(res_44999, 0);
        bool eq_x_zz_45005 = 0 == res_44999;
        bool not_p_45006 = !bounds_invalid_upwards_45004;
        bool p_and_eq_x_y_45007 = eq_x_zz_45005 && not_p_45006;
        bool dim_zzero_45008 = bounds_invalid_upwards_45004 ||
             p_and_eq_x_y_45007;
        bool both_empty_45009 = eq_x_zz_45005 && dim_zzero_45008;
        bool eq_x_y_45010 = res_44999 == 0;
        bool empty_or_match_45013 = not_p_45006 || both_empty_45009;
        bool empty_or_match_cert_45014;
        
        if (!empty_or_match_45013) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:174:1-182:55 -> tupleTest.fut:180:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                                   "Function return value does not match shape of type ",
                                   "*", "[", res_44999, "]", "t");
            if (memblock_unref(ctx, &mem_48057, "mem_48057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_48046, "mem_48046") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_48044, "mem_48044") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_48002, "xs_mem_48002") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_48000, "xs_mem_48000") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47998, "xs_mem_47998") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_48041, "indexed_mem_48041") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_48039, "indexed_mem_48039") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_48037, "indexed_mem_48037") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_48063 = sext_i32_i64(res_44999);
        int64_t bytes_48062 = 4 * binop_x_48063;
        struct memblock mem_48064;
        
        mem_48064.references = NULL;
        if (memblock_alloc(ctx, &mem_48064, bytes_48062, "mem_48064"))
            return 1;
        for (int32_t i_48494 = 0; i_48494 < res_44999; i_48494++) {
            *(int32_t *) &mem_48064.mem[i_48494 * 4] = 1;
        }
        for (int32_t write_iter_46115 = 0; write_iter_46115 < conc_tmp_44751;
             write_iter_46115++) {
            int32_t write_iv_46119 =
                    *(int32_t *) &mem_48057.mem[write_iter_46115 * 4];
            int32_t i_p_o_46637 = 1 + write_iter_46115;
            int32_t rot_i_46638 = smod32(i_p_o_46637, conc_tmp_44751);
            bool write_iv_46120 = *(bool *) &mem_48046.mem[rot_i_46638];
            int32_t res_45026;
            
            if (write_iv_46120) {
                int32_t res_45027 = write_iv_46119 - 1;
                
                res_45026 = res_45027;
            } else {
                res_45026 = -1;
            }
            
            bool less_than_zzero_46136 = slt32(res_45026, 0);
            bool greater_than_sizze_46137 = sle32(res_44999, res_45026);
            bool outside_bounds_dim_46138 = less_than_zzero_46136 ||
                 greater_than_sizze_46137;
            
            if (!outside_bounds_dim_46138) {
                memmove(mem_48064.mem + res_45026 * 4, mem_48044.mem +
                        write_iter_46115 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_48044, "mem_48044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48046, "mem_48046") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48057, "mem_48057") != 0)
            return 1;
        
        struct memblock mem_48071;
        
        mem_48071.references = NULL;
        if (memblock_alloc(ctx, &mem_48071, bytes_48062, "mem_48071"))
            return 1;
        
        struct memblock mem_48074;
        
        mem_48074.references = NULL;
        if (memblock_alloc(ctx, &mem_48074, bytes_48062, "mem_48074"))
            return 1;
        
        int32_t discard_46150;
        int32_t scanacc_46144 = 0;
        
        for (int32_t i_46147 = 0; i_46147 < res_44999; i_46147++) {
            int32_t x_45033 = *(int32_t *) &mem_48064.mem[i_46147 * 4];
            bool not_arg_45034 = x_45033 == 0;
            bool res_45035 = !not_arg_45034;
            int32_t part_res_45036;
            
            if (res_45035) {
                part_res_45036 = 0;
            } else {
                part_res_45036 = 1;
            }
            
            int32_t part_res_45037;
            
            if (res_45035) {
                part_res_45037 = 1;
            } else {
                part_res_45037 = 0;
            }
            
            int32_t zz_45032 = part_res_45037 + scanacc_46144;
            
            *(int32_t *) &mem_48071.mem[i_46147 * 4] = zz_45032;
            *(int32_t *) &mem_48074.mem[i_46147 * 4] = part_res_45036;
            
            int32_t scanacc_tmp_48496 = zz_45032;
            
            scanacc_46144 = scanacc_tmp_48496;
        }
        discard_46150 = scanacc_46144;
        
        int32_t last_index_45038 = res_44999 - 1;
        int32_t partition_sizze_45039;
        
        if (eq_x_y_45010) {
            partition_sizze_45039 = 0;
        } else {
            int32_t last_offset_45040 =
                    *(int32_t *) &mem_48071.mem[last_index_45038 * 4];
            
            partition_sizze_45039 = last_offset_45040;
        }
        
        int64_t binop_x_48084 = sext_i32_i64(partition_sizze_45039);
        int64_t bytes_48083 = 4 * binop_x_48084;
        struct memblock mem_48085;
        
        mem_48085.references = NULL;
        if (memblock_alloc(ctx, &mem_48085, bytes_48083, "mem_48085"))
            return 1;
        for (int32_t write_iter_46151 = 0; write_iter_46151 < res_44999;
             write_iter_46151++) {
            int32_t write_iv_46153 =
                    *(int32_t *) &mem_48074.mem[write_iter_46151 * 4];
            int32_t write_iv_46154 =
                    *(int32_t *) &mem_48071.mem[write_iter_46151 * 4];
            bool is_this_one_45048 = write_iv_46153 == 0;
            int32_t this_offset_45049 = -1 + write_iv_46154;
            int32_t total_res_45050;
            
            if (is_this_one_45048) {
                total_res_45050 = this_offset_45049;
            } else {
                total_res_45050 = -1;
            }
            
            bool less_than_zzero_46158 = slt32(total_res_45050, 0);
            bool greater_than_sizze_46159 = sle32(partition_sizze_45039,
                                                  total_res_45050);
            bool outside_bounds_dim_46160 = less_than_zzero_46158 ||
                 greater_than_sizze_46159;
            
            if (!outside_bounds_dim_46160) {
                memmove(mem_48085.mem + total_res_45050 * 4, mem_48064.mem +
                        write_iter_46151 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_48064, "mem_48064") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48071, "mem_48071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48074, "mem_48074") != 0)
            return 1;
        
        bool cond_45051 = partition_sizze_45039 == 4;
        bool res_45052;
        
        if (cond_45051) {
            bool res_45053;
            bool redout_46164 = 1;
            
            for (int32_t i_46165 = 0; i_46165 < partition_sizze_45039;
                 i_46165++) {
                int32_t x_45057 = *(int32_t *) &mem_48085.mem[i_46165 * 4];
                bool res_45058 = x_45057 == 1;
                bool x_45056 = res_45058 && redout_46164;
                bool redout_tmp_48500 = x_45056;
                
                redout_46164 = redout_tmp_48500;
            }
            res_45053 = redout_46164;
            res_45052 = res_45053;
        } else {
            res_45052 = 0;
        }
        if (memblock_unref(ctx, &mem_48085, "mem_48085") != 0)
            return 1;
        res_44668 = res_45052;
        if (memblock_unref(ctx, &mem_48085, "mem_48085") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48074, "mem_48074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48071, "mem_48071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48064, "mem_48064") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48057, "mem_48057") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48046, "mem_48046") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_48044, "mem_48044") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_48002, "xs_mem_48002") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_48000, "xs_mem_48000") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_47998, "xs_mem_47998") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_48041, "indexed_mem_48041") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_48039, "indexed_mem_48039") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_48037, "indexed_mem_48037") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47996, "res_mem_47996") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47994, "res_mem_47994") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_47992, "res_mem_47992") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47948, "mem_47948") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47945, "mem_47945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47942, "mem_47942") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47924, "mem_47924") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47921, "mem_47921") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47918, "mem_47918") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47907, "mem_47907") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47904, "mem_47904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47899, "mem_47899") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47894, "mem_47894") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47876, "mem_47876") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47873, "mem_47873") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47870, "mem_47870") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47851, "mem_47851") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47848, "mem_47848") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47845, "mem_47845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47842, "mem_47842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47837, "mem_47837") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_47832, "mem_47832") != 0)
            return 1;
    } else {
        res_44668 = 0;
    }
    
    bool x_45059 = cond_43066 && res_43369;
    bool x_45060 = res_43528 && x_45059;
    bool x_45061 = res_43597 && x_45060;
    bool x_45062 = res_44668 && x_45061;
    
    scalar_out_48090 = x_45062;
    *out_scalar_out_48501 = scalar_out_48090;
    if (memblock_unref(ctx, &mem_47423, "mem_47423") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47412, "mem_47412") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47409, "mem_47409") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47402, "mem_47402") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47395, "mem_47395") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47384, "mem_47384") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47382, "mem_47382") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_47340, "xs_mem_47340") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_47338, "xs_mem_47338") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_47336, "xs_mem_47336") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_47379, "indexed_mem_47379") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_47377, "indexed_mem_47377") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_47375, "indexed_mem_47375") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_47334, "res_mem_47334") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_47332, "res_mem_47332") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_47330, "res_mem_47330") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47286, "mem_47286") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47283, "mem_47283") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47280, "mem_47280") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47259, "mem_47259") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47256, "mem_47256") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47253, "mem_47253") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47242, "mem_47242") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47239, "mem_47239") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47234, "mem_47234") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47229, "mem_47229") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47198, "mem_47198") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47180, "mem_47180") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47177, "mem_47177") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47174, "mem_47174") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47167, "mem_47167") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47162, "mem_47162") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47157, "mem_47157") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47078, "mem_47078") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47057, "mem_47057") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47054, "mem_47054") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47051, "mem_47051") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47040, "mem_47040") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47037, "mem_47037") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47032, "mem_47032") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_47027, "mem_47027") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46904, "mem_46904") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46901, "mem_46901") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46898, "mem_46898") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_46895, "res_mem_46895") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_46893, "res_mem_46893") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_46891, "res_mem_46891") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46877, "mem_46877") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46874, "mem_46874") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46871, "mem_46871") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46850, "mem_46850") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46847, "mem_46847") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46844, "mem_46844") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46833, "mem_46833") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46830, "mem_46830") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46825, "mem_46825") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46820, "mem_46820") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46817, "mem_46817") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46705, "mem_46705") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46702, "mem_46702") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46699, "mem_46699") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46696, "mem_46696") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46693, "mem_46693") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46675, "mem_46675") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46672, "mem_46672") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46669, "mem_46669") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46658, "mem_46658") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46655, "mem_46655") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46650, "mem_46650") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46645, "mem_46645") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_46642, "mem_46642") != 0)
        return 1;
    return 0;
}
int futhark_entry_main(struct futhark_context *ctx, bool *out0)
{
    bool scalar_out_48090;
    
    lock_lock(&ctx->lock);
    
    int ret = futrts_main(ctx, &scalar_out_48090);
    
    if (ret == 0) {
        *out0 = scalar_out_48090;
    }
    lock_unlock(&ctx->lock);
    return ret;
}
