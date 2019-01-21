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
    bool result_64625;
    
    if (perform_warmup) {
        time_runs = 0;
        
        int r;
        
        assert(futhark_context_sync(ctx) == 0);
        t_start = get_wall_time();
        r = futhark_entry_main(ctx, &result_64625);
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
        r = futhark_entry_main(ctx, &result_64625);
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
    write_scalar(stdout, binary_output, &bool_info, &result_64625);
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

static int32_t static_array_realtype_64616[4] = {1, 2, 3, 4};
static int32_t static_array_realtype_64617[4] = {0, 0, 1, 1};
static int32_t static_array_realtype_64618[4] = {0, 1, 0, 1};
static int32_t static_array_realtype_64619[3] = {0, 1, 1};
static int32_t static_array_realtype_64620[3] = {1, 0, 1};
static int32_t static_array_realtype_64621[3] = {1, 2, 3};
static int32_t static_array_realtype_64622[3] = {1, 4, 3};
static int32_t static_array_realtype_64623[4] = {0, 1, 1, 0};
static int32_t static_array_realtype_64624[4] = {1, 0, 1, 0};
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
    struct memblock static_array_64130;
    struct memblock static_array_64141;
    struct memblock static_array_64142;
    struct memblock static_array_64146;
    struct memblock static_array_64147;
    struct memblock static_array_64148;
    struct memblock static_array_64198;
    struct memblock static_array_64202;
    struct memblock static_array_64203;
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
    ctx->static_array_64130 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64616,
                                                 0};
    ctx->static_array_64141 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64617,
                                                 0};
    ctx->static_array_64142 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64618,
                                                 0};
    ctx->static_array_64146 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64619,
                                                 0};
    ctx->static_array_64147 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64620,
                                                 0};
    ctx->static_array_64148 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64621,
                                                 0};
    ctx->static_array_64198 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64622,
                                                 0};
    ctx->static_array_64202 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64623,
                                                 0};
    ctx->static_array_64203 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_64624,
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
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_64615);
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
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_64615)
{
    bool scalar_out_64129;
    struct memblock mem_62430;
    
    mem_62430.references = NULL;
    if (memblock_alloc(ctx, &mem_62430, 16, "mem_62430"))
        return 1;
    
    struct memblock static_array_64130 = ctx->static_array_64130;
    
    memmove(mem_62430.mem + 0, static_array_64130.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_62433;
    
    mem_62433.references = NULL;
    if (memblock_alloc(ctx, &mem_62433, 16, "mem_62433"))
        return 1;
    
    struct memblock mem_62438;
    
    mem_62438.references = NULL;
    if (memblock_alloc(ctx, &mem_62438, 8, "mem_62438"))
        return 1;
    for (int32_t i_60523 = 0; i_60523 < 2; i_60523++) {
        for (int32_t i_64132 = 0; i_64132 < 2; i_64132++) {
            *(int32_t *) &mem_62438.mem[i_64132 * 4] = i_60523;
        }
        memmove(mem_62433.mem + 2 * i_60523 * 4, mem_62438.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
        return 1;
    
    struct memblock mem_62443;
    
    mem_62443.references = NULL;
    if (memblock_alloc(ctx, &mem_62443, 16, "mem_62443"))
        return 1;
    
    struct memblock mem_62446;
    
    mem_62446.references = NULL;
    if (memblock_alloc(ctx, &mem_62446, 16, "mem_62446"))
        return 1;
    
    int32_t discard_60533;
    int32_t scanacc_60527 = 0;
    
    for (int32_t i_60530 = 0; i_60530 < 4; i_60530++) {
        int32_t zz_57786 = 1 + scanacc_60527;
        
        *(int32_t *) &mem_62443.mem[i_60530 * 4] = zz_57786;
        *(int32_t *) &mem_62446.mem[i_60530 * 4] = 0;
        
        int32_t scanacc_tmp_64133 = zz_57786;
        
        scanacc_60527 = scanacc_tmp_64133;
    }
    discard_60533 = scanacc_60527;
    
    int32_t last_offset_57792 = *(int32_t *) &mem_62443.mem[12];
    int64_t binop_x_62456 = sext_i32_i64(last_offset_57792);
    int64_t bytes_62455 = 4 * binop_x_62456;
    struct memblock mem_62457;
    
    mem_62457.references = NULL;
    if (memblock_alloc(ctx, &mem_62457, bytes_62455, "mem_62457"))
        return 1;
    
    struct memblock mem_62460;
    
    mem_62460.references = NULL;
    if (memblock_alloc(ctx, &mem_62460, bytes_62455, "mem_62460"))
        return 1;
    
    struct memblock mem_62463;
    
    mem_62463.references = NULL;
    if (memblock_alloc(ctx, &mem_62463, bytes_62455, "mem_62463"))
        return 1;
    for (int32_t write_iter_60534 = 0; write_iter_60534 < 4;
         write_iter_60534++) {
        int32_t write_iv_60538 = *(int32_t *) &mem_62446.mem[write_iter_60534 *
                                                             4];
        int32_t write_iv_60539 = *(int32_t *) &mem_62443.mem[write_iter_60534 *
                                                             4];
        int32_t new_index_61880 = squot32(write_iter_60534, 2);
        int32_t binop_y_61882 = 2 * new_index_61880;
        int32_t new_index_61883 = write_iter_60534 - binop_y_61882;
        bool is_this_one_57804 = write_iv_60538 == 0;
        int32_t this_offset_57805 = -1 + write_iv_60539;
        int32_t total_res_57806;
        
        if (is_this_one_57804) {
            total_res_57806 = this_offset_57805;
        } else {
            total_res_57806 = -1;
        }
        
        bool less_than_zzero_60543 = slt32(total_res_57806, 0);
        bool greater_than_sizze_60544 = sle32(last_offset_57792,
                                              total_res_57806);
        bool outside_bounds_dim_60545 = less_than_zzero_60543 ||
             greater_than_sizze_60544;
        
        if (!outside_bounds_dim_60545) {
            memmove(mem_62457.mem + total_res_57806 * 4, mem_62433.mem + (2 *
                                                                          new_index_61880 +
                                                                          new_index_61883) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_60545) {
            struct memblock mem_62472;
            
            mem_62472.references = NULL;
            if (memblock_alloc(ctx, &mem_62472, 4, "mem_62472"))
                return 1;
            
            int32_t x_64140;
            
            for (int32_t i_64139 = 0; i_64139 < 1; i_64139++) {
                x_64140 = new_index_61883 + sext_i32_i32(i_64139);
                *(int32_t *) &mem_62472.mem[i_64139 * 4] = x_64140;
            }
            memmove(mem_62460.mem + total_res_57806 * 4, mem_62472.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_62472, "mem_62472") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62472, "mem_62472") != 0)
                return 1;
        }
        if (!outside_bounds_dim_60545) {
            memmove(mem_62463.mem + total_res_57806 * 4, mem_62430.mem +
                    write_iter_60534 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
        return 1;
    
    struct memblock mem_62481;
    
    mem_62481.references = NULL;
    if (memblock_alloc(ctx, &mem_62481, 16, "mem_62481"))
        return 1;
    
    struct memblock static_array_64141 = ctx->static_array_64141;
    
    memmove(mem_62481.mem + 0, static_array_64141.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_62484;
    
    mem_62484.references = NULL;
    if (memblock_alloc(ctx, &mem_62484, 16, "mem_62484"))
        return 1;
    
    struct memblock static_array_64142 = ctx->static_array_64142;
    
    memmove(mem_62484.mem + 0, static_array_64142.mem + 0, 4 * sizeof(int32_t));
    
    bool dim_eq_57809 = last_offset_57792 == 4;
    bool arrays_equal_57810;
    
    if (dim_eq_57809) {
        bool all_equal_57812;
        bool redout_60561 = 1;
        
        for (int32_t i_60562 = 0; i_60562 < last_offset_57792; i_60562++) {
            int32_t x_57816 = *(int32_t *) &mem_62457.mem[i_60562 * 4];
            int32_t y_57817 = *(int32_t *) &mem_62481.mem[i_60562 * 4];
            bool res_57818 = x_57816 == y_57817;
            bool res_57815 = res_57818 && redout_60561;
            bool redout_tmp_64143 = res_57815;
            
            redout_60561 = redout_tmp_64143;
        }
        all_equal_57812 = redout_60561;
        arrays_equal_57810 = all_equal_57812;
    } else {
        arrays_equal_57810 = 0;
    }
    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
        return 1;
    
    bool arrays_equal_57819;
    
    if (dim_eq_57809) {
        bool all_equal_57821;
        bool redout_60563 = 1;
        
        for (int32_t i_60564 = 0; i_60564 < last_offset_57792; i_60564++) {
            int32_t x_57825 = *(int32_t *) &mem_62460.mem[i_60564 * 4];
            int32_t y_57826 = *(int32_t *) &mem_62484.mem[i_60564 * 4];
            bool res_57827 = x_57825 == y_57826;
            bool res_57824 = res_57827 && redout_60563;
            bool redout_tmp_64144 = res_57824;
            
            redout_60563 = redout_tmp_64144;
        }
        all_equal_57821 = redout_60563;
        arrays_equal_57819 = all_equal_57821;
    } else {
        arrays_equal_57819 = 0;
    }
    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
        return 1;
    
    bool eq_57828 = arrays_equal_57810 && arrays_equal_57819;
    bool res_57829;
    
    if (eq_57828) {
        bool arrays_equal_57830;
        
        if (dim_eq_57809) {
            bool all_equal_57832;
            bool redout_60565 = 1;
            
            for (int32_t i_60566 = 0; i_60566 < last_offset_57792; i_60566++) {
                int32_t x_57836 = *(int32_t *) &mem_62463.mem[i_60566 * 4];
                int32_t y_57837 = *(int32_t *) &mem_62430.mem[i_60566 * 4];
                bool res_57838 = x_57836 == y_57837;
                bool res_57835 = res_57838 && redout_60565;
                bool redout_tmp_64145 = res_57835;
                
                redout_60565 = redout_tmp_64145;
            }
            all_equal_57832 = redout_60565;
            arrays_equal_57830 = all_equal_57832;
        } else {
            arrays_equal_57830 = 0;
        }
        res_57829 = arrays_equal_57830;
    } else {
        res_57829 = 0;
    }
    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
        return 1;
    
    struct memblock mem_62487;
    
    mem_62487.references = NULL;
    if (memblock_alloc(ctx, &mem_62487, 12, "mem_62487"))
        return 1;
    
    struct memblock static_array_64146 = ctx->static_array_64146;
    
    memmove(mem_62487.mem + 0, static_array_64146.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_62490;
    
    mem_62490.references = NULL;
    if (memblock_alloc(ctx, &mem_62490, 12, "mem_62490"))
        return 1;
    
    struct memblock static_array_64147 = ctx->static_array_64147;
    
    memmove(mem_62490.mem + 0, static_array_64147.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_62493;
    
    mem_62493.references = NULL;
    if (memblock_alloc(ctx, &mem_62493, 12, "mem_62493"))
        return 1;
    
    struct memblock static_array_64148 = ctx->static_array_64148;
    
    memmove(mem_62493.mem + 0, static_array_64148.mem + 0, 3 * sizeof(int32_t));
    
    bool cond_57843;
    
    if (res_57829) {
        struct memblock mem_62496;
        
        mem_62496.references = NULL;
        if (memblock_alloc(ctx, &mem_62496, 16, "mem_62496"))
            return 1;
        
        struct memblock mem_62501;
        
        mem_62501.references = NULL;
        if (memblock_alloc(ctx, &mem_62501, 8, "mem_62501"))
            return 1;
        for (int32_t i_60569 = 0; i_60569 < 2; i_60569++) {
            for (int32_t i_64150 = 0; i_64150 < 2; i_64150++) {
                *(int32_t *) &mem_62501.mem[i_64150 * 4] = i_60569;
            }
            memmove(mem_62496.mem + 2 * i_60569 * 4, mem_62501.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_62501, "mem_62501") != 0)
            return 1;
        
        struct memblock mem_62506;
        
        mem_62506.references = NULL;
        if (memblock_alloc(ctx, &mem_62506, 16, "mem_62506"))
            return 1;
        
        struct memblock mem_62509;
        
        mem_62509.references = NULL;
        if (memblock_alloc(ctx, &mem_62509, 16, "mem_62509"))
            return 1;
        
        int32_t discard_60579;
        int32_t scanacc_60573 = 0;
        
        for (int32_t i_60576 = 0; i_60576 < 4; i_60576++) {
            bool not_arg_57854 = i_60576 == 0;
            bool res_57855 = !not_arg_57854;
            int32_t part_res_57856;
            
            if (res_57855) {
                part_res_57856 = 0;
            } else {
                part_res_57856 = 1;
            }
            
            int32_t part_res_57857;
            
            if (res_57855) {
                part_res_57857 = 1;
            } else {
                part_res_57857 = 0;
            }
            
            int32_t zz_57852 = part_res_57857 + scanacc_60573;
            
            *(int32_t *) &mem_62506.mem[i_60576 * 4] = zz_57852;
            *(int32_t *) &mem_62509.mem[i_60576 * 4] = part_res_57856;
            
            int32_t scanacc_tmp_64151 = zz_57852;
            
            scanacc_60573 = scanacc_tmp_64151;
        }
        discard_60579 = scanacc_60573;
        
        int32_t last_offset_57858 = *(int32_t *) &mem_62506.mem[12];
        int64_t binop_x_62519 = sext_i32_i64(last_offset_57858);
        int64_t bytes_62518 = 4 * binop_x_62519;
        struct memblock mem_62520;
        
        mem_62520.references = NULL;
        if (memblock_alloc(ctx, &mem_62520, bytes_62518, "mem_62520"))
            return 1;
        
        struct memblock mem_62523;
        
        mem_62523.references = NULL;
        if (memblock_alloc(ctx, &mem_62523, bytes_62518, "mem_62523"))
            return 1;
        
        struct memblock mem_62526;
        
        mem_62526.references = NULL;
        if (memblock_alloc(ctx, &mem_62526, bytes_62518, "mem_62526"))
            return 1;
        for (int32_t write_iter_60580 = 0; write_iter_60580 < 4;
             write_iter_60580++) {
            int32_t write_iv_60584 =
                    *(int32_t *) &mem_62509.mem[write_iter_60580 * 4];
            int32_t write_iv_60585 =
                    *(int32_t *) &mem_62506.mem[write_iter_60580 * 4];
            int32_t new_index_61896 = squot32(write_iter_60580, 2);
            int32_t binop_y_61898 = 2 * new_index_61896;
            int32_t new_index_61899 = write_iter_60580 - binop_y_61898;
            bool is_this_one_57870 = write_iv_60584 == 0;
            int32_t this_offset_57871 = -1 + write_iv_60585;
            int32_t total_res_57872;
            
            if (is_this_one_57870) {
                total_res_57872 = this_offset_57871;
            } else {
                total_res_57872 = -1;
            }
            
            bool less_than_zzero_60589 = slt32(total_res_57872, 0);
            bool greater_than_sizze_60590 = sle32(last_offset_57858,
                                                  total_res_57872);
            bool outside_bounds_dim_60591 = less_than_zzero_60589 ||
                 greater_than_sizze_60590;
            
            if (!outside_bounds_dim_60591) {
                memmove(mem_62520.mem + total_res_57872 * 4, mem_62496.mem +
                        (2 * new_index_61896 + new_index_61899) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_60591) {
                struct memblock mem_62535;
                
                mem_62535.references = NULL;
                if (memblock_alloc(ctx, &mem_62535, 4, "mem_62535"))
                    return 1;
                
                int32_t x_64158;
                
                for (int32_t i_64157 = 0; i_64157 < 1; i_64157++) {
                    x_64158 = new_index_61899 + sext_i32_i32(i_64157);
                    *(int32_t *) &mem_62535.mem[i_64157 * 4] = x_64158;
                }
                memmove(mem_62523.mem + total_res_57872 * 4, mem_62535.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62535, "mem_62535") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62535, "mem_62535") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_60591) {
                struct memblock mem_62538;
                
                mem_62538.references = NULL;
                if (memblock_alloc(ctx, &mem_62538, 4, "mem_62538"))
                    return 1;
                
                int32_t x_64160;
                
                for (int32_t i_64159 = 0; i_64159 < 1; i_64159++) {
                    x_64160 = write_iter_60580 + sext_i32_i32(i_64159);
                    *(int32_t *) &mem_62538.mem[i_64159 * 4] = x_64160;
                }
                memmove(mem_62526.mem + total_res_57872 * 4, mem_62538.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62538, "mem_62538") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62538, "mem_62538") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_62496, "mem_62496") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62506, "mem_62506") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62509, "mem_62509") != 0)
            return 1;
        
        bool dim_eq_57873 = last_offset_57858 == 3;
        bool arrays_equal_57874;
        
        if (dim_eq_57873) {
            bool all_equal_57876;
            bool redout_60607 = 1;
            
            for (int32_t i_60608 = 0; i_60608 < last_offset_57858; i_60608++) {
                int32_t x_57880 = *(int32_t *) &mem_62520.mem[i_60608 * 4];
                int32_t y_57881 = *(int32_t *) &mem_62487.mem[i_60608 * 4];
                bool res_57882 = x_57880 == y_57881;
                bool res_57879 = res_57882 && redout_60607;
                bool redout_tmp_64161 = res_57879;
                
                redout_60607 = redout_tmp_64161;
            }
            all_equal_57876 = redout_60607;
            arrays_equal_57874 = all_equal_57876;
        } else {
            arrays_equal_57874 = 0;
        }
        if (memblock_unref(ctx, &mem_62520, "mem_62520") != 0)
            return 1;
        
        bool arrays_equal_57883;
        
        if (dim_eq_57873) {
            bool all_equal_57885;
            bool redout_60609 = 1;
            
            for (int32_t i_60610 = 0; i_60610 < last_offset_57858; i_60610++) {
                int32_t x_57889 = *(int32_t *) &mem_62523.mem[i_60610 * 4];
                int32_t y_57890 = *(int32_t *) &mem_62490.mem[i_60610 * 4];
                bool res_57891 = x_57889 == y_57890;
                bool res_57888 = res_57891 && redout_60609;
                bool redout_tmp_64162 = res_57888;
                
                redout_60609 = redout_tmp_64162;
            }
            all_equal_57885 = redout_60609;
            arrays_equal_57883 = all_equal_57885;
        } else {
            arrays_equal_57883 = 0;
        }
        if (memblock_unref(ctx, &mem_62523, "mem_62523") != 0)
            return 1;
        
        bool eq_57892 = arrays_equal_57874 && arrays_equal_57883;
        bool res_57893;
        
        if (eq_57892) {
            bool arrays_equal_57894;
            
            if (dim_eq_57873) {
                bool all_equal_57896;
                bool redout_60611 = 1;
                
                for (int32_t i_60612 = 0; i_60612 < last_offset_57858;
                     i_60612++) {
                    int32_t x_57900 = *(int32_t *) &mem_62526.mem[i_60612 * 4];
                    int32_t y_57901 = *(int32_t *) &mem_62493.mem[i_60612 * 4];
                    bool res_57902 = x_57900 == y_57901;
                    bool res_57899 = res_57902 && redout_60611;
                    bool redout_tmp_64163 = res_57899;
                    
                    redout_60611 = redout_tmp_64163;
                }
                all_equal_57896 = redout_60611;
                arrays_equal_57894 = all_equal_57896;
            } else {
                arrays_equal_57894 = 0;
            }
            res_57893 = arrays_equal_57894;
        } else {
            res_57893 = 0;
        }
        if (memblock_unref(ctx, &mem_62526, "mem_62526") != 0)
            return 1;
        cond_57843 = res_57893;
        if (memblock_unref(ctx, &mem_62526, "mem_62526") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62523, "mem_62523") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62520, "mem_62520") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62509, "mem_62509") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62506, "mem_62506") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62501, "mem_62501") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62496, "mem_62496") != 0)
            return 1;
    } else {
        cond_57843 = 0;
    }
    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
        return 1;
    
    bool cond_57903;
    
    if (cond_57843) {
        struct memblock mem_62547;
        
        mem_62547.references = NULL;
        if (memblock_alloc(ctx, &mem_62547, 16, "mem_62547"))
            return 1;
        
        struct memblock mem_62552;
        
        mem_62552.references = NULL;
        if (memblock_alloc(ctx, &mem_62552, 8, "mem_62552"))
            return 1;
        for (int32_t i_60615 = 0; i_60615 < 2; i_60615++) {
            for (int32_t i_64165 = 0; i_64165 < 2; i_64165++) {
                *(int32_t *) &mem_62552.mem[i_64165 * 4] = i_60615;
            }
            memmove(mem_62547.mem + 2 * i_60615 * 4, mem_62552.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_62552, "mem_62552") != 0)
            return 1;
        
        struct memblock mem_62557;
        
        mem_62557.references = NULL;
        if (memblock_alloc(ctx, &mem_62557, 16, "mem_62557"))
            return 1;
        
        struct memblock mem_62560;
        
        mem_62560.references = NULL;
        if (memblock_alloc(ctx, &mem_62560, 16, "mem_62560"))
            return 1;
        
        int32_t discard_60625;
        int32_t scanacc_60619 = 0;
        
        for (int32_t i_60622 = 0; i_60622 < 4; i_60622++) {
            bool not_arg_57914 = i_60622 == 0;
            bool res_57915 = !not_arg_57914;
            int32_t part_res_57916;
            
            if (res_57915) {
                part_res_57916 = 0;
            } else {
                part_res_57916 = 1;
            }
            
            int32_t part_res_57917;
            
            if (res_57915) {
                part_res_57917 = 1;
            } else {
                part_res_57917 = 0;
            }
            
            int32_t zz_57912 = part_res_57917 + scanacc_60619;
            
            *(int32_t *) &mem_62557.mem[i_60622 * 4] = zz_57912;
            *(int32_t *) &mem_62560.mem[i_60622 * 4] = part_res_57916;
            
            int32_t scanacc_tmp_64166 = zz_57912;
            
            scanacc_60619 = scanacc_tmp_64166;
        }
        discard_60625 = scanacc_60619;
        
        int32_t last_offset_57918 = *(int32_t *) &mem_62557.mem[12];
        int64_t binop_x_62570 = sext_i32_i64(last_offset_57918);
        int64_t bytes_62569 = 4 * binop_x_62570;
        struct memblock mem_62571;
        
        mem_62571.references = NULL;
        if (memblock_alloc(ctx, &mem_62571, bytes_62569, "mem_62571"))
            return 1;
        
        struct memblock mem_62574;
        
        mem_62574.references = NULL;
        if (memblock_alloc(ctx, &mem_62574, bytes_62569, "mem_62574"))
            return 1;
        
        struct memblock mem_62577;
        
        mem_62577.references = NULL;
        if (memblock_alloc(ctx, &mem_62577, bytes_62569, "mem_62577"))
            return 1;
        for (int32_t write_iter_60626 = 0; write_iter_60626 < 4;
             write_iter_60626++) {
            int32_t write_iv_60630 =
                    *(int32_t *) &mem_62560.mem[write_iter_60626 * 4];
            int32_t write_iv_60631 =
                    *(int32_t *) &mem_62557.mem[write_iter_60626 * 4];
            int32_t new_index_61915 = squot32(write_iter_60626, 2);
            int32_t binop_y_61917 = 2 * new_index_61915;
            int32_t new_index_61918 = write_iter_60626 - binop_y_61917;
            bool is_this_one_57930 = write_iv_60630 == 0;
            int32_t this_offset_57931 = -1 + write_iv_60631;
            int32_t total_res_57932;
            
            if (is_this_one_57930) {
                total_res_57932 = this_offset_57931;
            } else {
                total_res_57932 = -1;
            }
            
            bool less_than_zzero_60635 = slt32(total_res_57932, 0);
            bool greater_than_sizze_60636 = sle32(last_offset_57918,
                                                  total_res_57932);
            bool outside_bounds_dim_60637 = less_than_zzero_60635 ||
                 greater_than_sizze_60636;
            
            if (!outside_bounds_dim_60637) {
                memmove(mem_62571.mem + total_res_57932 * 4, mem_62547.mem +
                        (2 * new_index_61915 + new_index_61918) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_60637) {
                struct memblock mem_62586;
                
                mem_62586.references = NULL;
                if (memblock_alloc(ctx, &mem_62586, 4, "mem_62586"))
                    return 1;
                
                int32_t x_64173;
                
                for (int32_t i_64172 = 0; i_64172 < 1; i_64172++) {
                    x_64173 = new_index_61918 + sext_i32_i32(i_64172);
                    *(int32_t *) &mem_62586.mem[i_64172 * 4] = x_64173;
                }
                memmove(mem_62574.mem + total_res_57932 * 4, mem_62586.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62586, "mem_62586") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62586, "mem_62586") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_60637) {
                struct memblock mem_62589;
                
                mem_62589.references = NULL;
                if (memblock_alloc(ctx, &mem_62589, 4, "mem_62589"))
                    return 1;
                
                int32_t x_64175;
                
                for (int32_t i_64174 = 0; i_64174 < 1; i_64174++) {
                    x_64175 = write_iter_60626 + sext_i32_i32(i_64174);
                    *(int32_t *) &mem_62589.mem[i_64174 * 4] = x_64175;
                }
                memmove(mem_62577.mem + total_res_57932 * 4, mem_62589.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62589, "mem_62589") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62589, "mem_62589") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_62547, "mem_62547") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62557, "mem_62557") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62560, "mem_62560") != 0)
            return 1;
        
        struct memblock mem_62598;
        
        mem_62598.references = NULL;
        if (memblock_alloc(ctx, &mem_62598, 16, "mem_62598"))
            return 1;
        for (int32_t i_64176 = 0; i_64176 < 4; i_64176++) {
            *(int32_t *) &mem_62598.mem[i_64176 * 4] = 0;
        }
        for (int32_t write_iter_60653 = 0; write_iter_60653 < last_offset_57918;
             write_iter_60653++) {
            int32_t write_iv_60655 =
                    *(int32_t *) &mem_62571.mem[write_iter_60653 * 4];
            int32_t write_iv_60656 =
                    *(int32_t *) &mem_62574.mem[write_iter_60653 * 4];
            int32_t x_57938 = 2 * write_iv_60655;
            int32_t res_57939 = x_57938 + write_iv_60656;
            bool less_than_zzero_60658 = slt32(res_57939, 0);
            bool greater_than_sizze_60659 = sle32(4, res_57939);
            bool outside_bounds_dim_60660 = less_than_zzero_60658 ||
                 greater_than_sizze_60659;
            
            if (!outside_bounds_dim_60660) {
                memmove(mem_62598.mem + res_57939 * 4, mem_62577.mem +
                        write_iter_60653 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_62571, "mem_62571") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62574, "mem_62574") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62577, "mem_62577") != 0)
            return 1;
        
        bool all_equal_57940;
        bool redout_60664 = 1;
        
        for (int32_t i_60665 = 0; i_60665 < 4; i_60665++) {
            int32_t y_57945 = *(int32_t *) &mem_62598.mem[i_60665 * 4];
            bool res_57946 = i_60665 == y_57945;
            bool res_57943 = res_57946 && redout_60664;
            bool redout_tmp_64178 = res_57943;
            
            redout_60664 = redout_tmp_64178;
        }
        all_equal_57940 = redout_60664;
        if (memblock_unref(ctx, &mem_62598, "mem_62598") != 0)
            return 1;
        cond_57903 = all_equal_57940;
        if (memblock_unref(ctx, &mem_62598, "mem_62598") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62577, "mem_62577") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62574, "mem_62574") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62571, "mem_62571") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62560, "mem_62560") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62557, "mem_62557") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62552, "mem_62552") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62547, "mem_62547") != 0)
            return 1;
    } else {
        cond_57903 = 0;
    }
    
    struct memblock mem_62605;
    
    mem_62605.references = NULL;
    if (memblock_alloc(ctx, &mem_62605, 0, "mem_62605"))
        return 1;
    
    struct memblock mem_62608;
    
    mem_62608.references = NULL;
    if (memblock_alloc(ctx, &mem_62608, 16, "mem_62608"))
        return 1;
    
    struct memblock mem_62613;
    
    mem_62613.references = NULL;
    if (memblock_alloc(ctx, &mem_62613, 8, "mem_62613"))
        return 1;
    for (int32_t i_60681 = 0; i_60681 < 2; i_60681++) {
        for (int32_t i_64180 = 0; i_64180 < 2; i_64180++) {
            *(int32_t *) &mem_62613.mem[i_64180 * 4] = i_60681;
        }
        memmove(mem_62608.mem + 2 * i_60681 * 4, mem_62613.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
        return 1;
    
    struct memblock mem_62618;
    
    mem_62618.references = NULL;
    if (memblock_alloc(ctx, &mem_62618, 16, "mem_62618"))
        return 1;
    
    struct memblock mem_62621;
    
    mem_62621.references = NULL;
    if (memblock_alloc(ctx, &mem_62621, 16, "mem_62621"))
        return 1;
    
    int32_t discard_60691;
    int32_t scanacc_60685 = 0;
    
    for (int32_t i_60688 = 0; i_60688 < 4; i_60688++) {
        bool not_arg_57975 = i_60688 == 0;
        bool res_57976 = !not_arg_57975;
        int32_t part_res_57977;
        
        if (res_57976) {
            part_res_57977 = 0;
        } else {
            part_res_57977 = 1;
        }
        
        int32_t part_res_57978;
        
        if (res_57976) {
            part_res_57978 = 1;
        } else {
            part_res_57978 = 0;
        }
        
        int32_t zz_57973 = part_res_57978 + scanacc_60685;
        
        *(int32_t *) &mem_62618.mem[i_60688 * 4] = zz_57973;
        *(int32_t *) &mem_62621.mem[i_60688 * 4] = part_res_57977;
        
        int32_t scanacc_tmp_64181 = zz_57973;
        
        scanacc_60685 = scanacc_tmp_64181;
    }
    discard_60691 = scanacc_60685;
    
    int32_t last_offset_57979 = *(int32_t *) &mem_62618.mem[12];
    int64_t binop_x_62631 = sext_i32_i64(last_offset_57979);
    int64_t bytes_62630 = 4 * binop_x_62631;
    struct memblock mem_62632;
    
    mem_62632.references = NULL;
    if (memblock_alloc(ctx, &mem_62632, bytes_62630, "mem_62632"))
        return 1;
    
    struct memblock mem_62635;
    
    mem_62635.references = NULL;
    if (memblock_alloc(ctx, &mem_62635, bytes_62630, "mem_62635"))
        return 1;
    
    struct memblock mem_62638;
    
    mem_62638.references = NULL;
    if (memblock_alloc(ctx, &mem_62638, bytes_62630, "mem_62638"))
        return 1;
    for (int32_t write_iter_60692 = 0; write_iter_60692 < 4;
         write_iter_60692++) {
        int32_t write_iv_60696 = *(int32_t *) &mem_62621.mem[write_iter_60692 *
                                                             4];
        int32_t write_iv_60697 = *(int32_t *) &mem_62618.mem[write_iter_60692 *
                                                             4];
        int32_t new_index_61945 = squot32(write_iter_60692, 2);
        int32_t binop_y_61947 = 2 * new_index_61945;
        int32_t new_index_61948 = write_iter_60692 - binop_y_61947;
        bool is_this_one_57991 = write_iv_60696 == 0;
        int32_t this_offset_57992 = -1 + write_iv_60697;
        int32_t total_res_57993;
        
        if (is_this_one_57991) {
            total_res_57993 = this_offset_57992;
        } else {
            total_res_57993 = -1;
        }
        
        bool less_than_zzero_60701 = slt32(total_res_57993, 0);
        bool greater_than_sizze_60702 = sle32(last_offset_57979,
                                              total_res_57993);
        bool outside_bounds_dim_60703 = less_than_zzero_60701 ||
             greater_than_sizze_60702;
        
        if (!outside_bounds_dim_60703) {
            memmove(mem_62632.mem + total_res_57993 * 4, mem_62608.mem + (2 *
                                                                          new_index_61945 +
                                                                          new_index_61948) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_60703) {
            struct memblock mem_62647;
            
            mem_62647.references = NULL;
            if (memblock_alloc(ctx, &mem_62647, 4, "mem_62647"))
                return 1;
            
            int32_t x_64188;
            
            for (int32_t i_64187 = 0; i_64187 < 1; i_64187++) {
                x_64188 = new_index_61948 + sext_i32_i32(i_64187);
                *(int32_t *) &mem_62647.mem[i_64187 * 4] = x_64188;
            }
            memmove(mem_62635.mem + total_res_57993 * 4, mem_62647.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_62647, "mem_62647") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62647, "mem_62647") != 0)
                return 1;
        }
        if (!outside_bounds_dim_60703) {
            struct memblock mem_62650;
            
            mem_62650.references = NULL;
            if (memblock_alloc(ctx, &mem_62650, 4, "mem_62650"))
                return 1;
            
            int32_t x_64190;
            
            for (int32_t i_64189 = 0; i_64189 < 1; i_64189++) {
                x_64190 = write_iter_60692 + sext_i32_i32(i_64189);
                *(int32_t *) &mem_62650.mem[i_64189 * 4] = x_64190;
            }
            memmove(mem_62638.mem + total_res_57993 * 4, mem_62650.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_62650, "mem_62650") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62650, "mem_62650") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
        return 1;
    
    struct memblock mem_62659;
    
    mem_62659.references = NULL;
    if (memblock_alloc(ctx, &mem_62659, 4, "mem_62659"))
        return 1;
    for (int32_t i_64191 = 0; i_64191 < 1; i_64191++) {
        *(int32_t *) &mem_62659.mem[i_64191 * 4] = 1;
    }
    
    struct memblock mem_62662;
    
    mem_62662.references = NULL;
    if (memblock_alloc(ctx, &mem_62662, 4, "mem_62662"))
        return 1;
    for (int32_t i_64192 = 0; i_64192 < 1; i_64192++) {
        *(int32_t *) &mem_62662.mem[i_64192 * 4] = 0;
    }
    
    struct memblock mem_62665;
    
    mem_62665.references = NULL;
    if (memblock_alloc(ctx, &mem_62665, 4, "mem_62665"))
        return 1;
    for (int32_t i_64193 = 0; i_64193 < 1; i_64193++) {
        *(int32_t *) &mem_62665.mem[i_64193 * 4] = 4;
    }
    
    int32_t conc_tmp_58007 = 1 + last_offset_57979;
    int32_t res_58018;
    int32_t redout_60719 = last_offset_57979;
    
    for (int32_t i_60720 = 0; i_60720 < last_offset_57979; i_60720++) {
        int32_t x_58022 = *(int32_t *) &mem_62632.mem[i_60720 * 4];
        int32_t x_58023 = *(int32_t *) &mem_62635.mem[i_60720 * 4];
        bool cond_58025 = x_58022 == 1;
        bool cond_58026 = x_58023 == 0;
        bool eq_58027 = cond_58025 && cond_58026;
        int32_t res_58028;
        
        if (eq_58027) {
            res_58028 = i_60720;
        } else {
            res_58028 = last_offset_57979;
        }
        
        int32_t res_58021 = smin32(res_58028, redout_60719);
        int32_t redout_tmp_64194 = res_58021;
        
        redout_60719 = redout_tmp_64194;
    }
    res_58018 = redout_60719;
    
    bool cond_58029 = res_58018 == last_offset_57979;
    int32_t res_58030;
    
    if (cond_58029) {
        res_58030 = -1;
    } else {
        res_58030 = res_58018;
    }
    
    bool eq_x_zz_58031 = -1 == res_58018;
    bool not_p_58032 = !cond_58029;
    bool p_and_eq_x_y_58033 = eq_x_zz_58031 && not_p_58032;
    bool cond_58034 = cond_58029 || p_and_eq_x_y_58033;
    bool cond_58035 = !cond_58034;
    int32_t sizze_58036;
    
    if (cond_58035) {
        sizze_58036 = last_offset_57979;
    } else {
        sizze_58036 = conc_tmp_58007;
    }
    
    int64_t binop_x_62670 = sext_i32_i64(conc_tmp_58007);
    int64_t bytes_62669 = 4 * binop_x_62670;
    int64_t res_mem_sizze_62678;
    struct memblock res_mem_62679;
    
    res_mem_62679.references = NULL;
    
    int64_t res_mem_sizze_62680;
    struct memblock res_mem_62681;
    
    res_mem_62681.references = NULL;
    
    int64_t res_mem_sizze_62682;
    struct memblock res_mem_62683;
    
    res_mem_62683.references = NULL;
    if (cond_58035) {
        struct memblock mem_62668;
        
        mem_62668.references = NULL;
        if (memblock_alloc(ctx, &mem_62668, bytes_62630, "mem_62668"))
            return 1;
        memmove(mem_62668.mem + 0, mem_62638.mem + 0, last_offset_57979 *
                sizeof(int32_t));
        
        bool less_than_zzero_60723 = slt32(res_58030, 0);
        bool greater_than_sizze_60724 = sle32(last_offset_57979, res_58030);
        bool outside_bounds_dim_60725 = less_than_zzero_60723 ||
             greater_than_sizze_60724;
        
        if (!outside_bounds_dim_60725) {
            *(int32_t *) &mem_62668.mem[res_58030 * 4] = 4;
        }
        res_mem_sizze_62678 = bytes_62630;
        if (memblock_set(ctx, &res_mem_62679, &mem_62632, "mem_62632") != 0)
            return 1;
        res_mem_sizze_62680 = bytes_62630;
        if (memblock_set(ctx, &res_mem_62681, &mem_62635, "mem_62635") != 0)
            return 1;
        res_mem_sizze_62682 = bytes_62630;
        if (memblock_set(ctx, &res_mem_62683, &mem_62668, "mem_62668") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62668, "mem_62668") != 0)
            return 1;
    } else {
        struct memblock mem_62671;
        
        mem_62671.references = NULL;
        if (memblock_alloc(ctx, &mem_62671, bytes_62669, "mem_62671"))
            return 1;
        
        int32_t tmp_offs_64195 = 0;
        
        memmove(mem_62671.mem + tmp_offs_64195 * 4, mem_62632.mem + 0,
                last_offset_57979 * sizeof(int32_t));
        tmp_offs_64195 += last_offset_57979;
        memmove(mem_62671.mem + tmp_offs_64195 * 4, mem_62659.mem + 0,
                sizeof(int32_t));
        tmp_offs_64195 += 1;
        
        struct memblock mem_62674;
        
        mem_62674.references = NULL;
        if (memblock_alloc(ctx, &mem_62674, bytes_62669, "mem_62674"))
            return 1;
        
        int32_t tmp_offs_64196 = 0;
        
        memmove(mem_62674.mem + tmp_offs_64196 * 4, mem_62635.mem + 0,
                last_offset_57979 * sizeof(int32_t));
        tmp_offs_64196 += last_offset_57979;
        memmove(mem_62674.mem + tmp_offs_64196 * 4, mem_62662.mem + 0,
                sizeof(int32_t));
        tmp_offs_64196 += 1;
        
        struct memblock mem_62677;
        
        mem_62677.references = NULL;
        if (memblock_alloc(ctx, &mem_62677, bytes_62669, "mem_62677"))
            return 1;
        
        int32_t tmp_offs_64197 = 0;
        
        memmove(mem_62677.mem + tmp_offs_64197 * 4, mem_62638.mem + 0,
                last_offset_57979 * sizeof(int32_t));
        tmp_offs_64197 += last_offset_57979;
        memmove(mem_62677.mem + tmp_offs_64197 * 4, mem_62665.mem + 0,
                sizeof(int32_t));
        tmp_offs_64197 += 1;
        res_mem_sizze_62678 = bytes_62669;
        if (memblock_set(ctx, &res_mem_62679, &mem_62671, "mem_62671") != 0)
            return 1;
        res_mem_sizze_62680 = bytes_62669;
        if (memblock_set(ctx, &res_mem_62681, &mem_62674, "mem_62674") != 0)
            return 1;
        res_mem_sizze_62682 = bytes_62669;
        if (memblock_set(ctx, &res_mem_62683, &mem_62677, "mem_62677") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62677, "mem_62677") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62674, "mem_62674") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62671, "mem_62671") != 0)
            return 1;
    }
    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
        return 1;
    
    struct memblock mem_62686;
    
    mem_62686.references = NULL;
    if (memblock_alloc(ctx, &mem_62686, 12, "mem_62686"))
        return 1;
    
    struct memblock static_array_64198 = ctx->static_array_64198;
    
    memmove(mem_62686.mem + 0, static_array_64198.mem + 0, 3 * sizeof(int32_t));
    
    bool eq_x_y_58052 = 3 == last_offset_57979;
    bool eq_x_zz_58053 = 3 == conc_tmp_58007;
    bool p_and_eq_x_y_58054 = cond_58035 && eq_x_y_58052;
    bool p_and_eq_x_y_58055 = cond_58034 && eq_x_zz_58053;
    bool dim_eq_58056 = p_and_eq_x_y_58054 || p_and_eq_x_y_58055;
    bool arrays_equal_58057;
    
    if (dim_eq_58056) {
        bool all_equal_58059;
        bool redout_60729 = 1;
        
        for (int32_t i_60730 = 0; i_60730 < sizze_58036; i_60730++) {
            int32_t x_58063 = *(int32_t *) &res_mem_62683.mem[i_60730 * 4];
            int32_t y_58064 = *(int32_t *) &mem_62686.mem[i_60730 * 4];
            bool res_58065 = x_58063 == y_58064;
            bool res_58062 = res_58065 && redout_60729;
            bool redout_tmp_64199 = res_58062;
            
            redout_60729 = redout_tmp_64199;
        }
        all_equal_58059 = redout_60729;
        arrays_equal_58057 = all_equal_58059;
    } else {
        arrays_equal_58057 = 0;
    }
    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
        return 1;
    
    bool res_58066;
    
    if (arrays_equal_58057) {
        bool arrays_equal_58067;
        
        if (dim_eq_58056) {
            bool all_equal_58069;
            bool redout_60731 = 1;
            
            for (int32_t i_60732 = 0; i_60732 < sizze_58036; i_60732++) {
                int32_t x_58073 = *(int32_t *) &res_mem_62679.mem[i_60732 * 4];
                int32_t y_58074 = *(int32_t *) &mem_62487.mem[i_60732 * 4];
                bool res_58075 = x_58073 == y_58074;
                bool res_58072 = res_58075 && redout_60731;
                bool redout_tmp_64200 = res_58072;
                
                redout_60731 = redout_tmp_64200;
            }
            all_equal_58069 = redout_60731;
            arrays_equal_58067 = all_equal_58069;
        } else {
            arrays_equal_58067 = 0;
        }
        
        bool arrays_equal_58076;
        
        if (dim_eq_58056) {
            bool all_equal_58078;
            bool redout_60733 = 1;
            
            for (int32_t i_60734 = 0; i_60734 < sizze_58036; i_60734++) {
                int32_t x_58082 = *(int32_t *) &res_mem_62681.mem[i_60734 * 4];
                int32_t y_58083 = *(int32_t *) &mem_62490.mem[i_60734 * 4];
                bool res_58084 = x_58082 == y_58083;
                bool res_58081 = res_58084 && redout_60733;
                bool redout_tmp_64201 = res_58081;
                
                redout_60733 = redout_tmp_64201;
            }
            all_equal_58078 = redout_60733;
            arrays_equal_58076 = all_equal_58078;
        } else {
            arrays_equal_58076 = 0;
        }
        
        bool eq_58085 = arrays_equal_58067 && arrays_equal_58076;
        
        res_58066 = eq_58085;
    } else {
        res_58066 = 0;
    }
    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
        return 1;
    
    struct memblock mem_62689;
    
    mem_62689.references = NULL;
    if (memblock_alloc(ctx, &mem_62689, 16, "mem_62689"))
        return 1;
    
    struct memblock static_array_64202 = ctx->static_array_64202;
    
    memmove(mem_62689.mem + 0, static_array_64202.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_62692;
    
    mem_62692.references = NULL;
    if (memblock_alloc(ctx, &mem_62692, 16, "mem_62692"))
        return 1;
    
    struct memblock static_array_64203 = ctx->static_array_64203;
    
    memmove(mem_62692.mem + 0, static_array_64203.mem + 0, 4 * sizeof(int32_t));
    
    bool cond_58088;
    
    if (res_58066) {
        struct memblock mem_62695;
        
        mem_62695.references = NULL;
        if (memblock_alloc(ctx, &mem_62695, 16, "mem_62695"))
            return 1;
        
        struct memblock mem_62700;
        
        mem_62700.references = NULL;
        if (memblock_alloc(ctx, &mem_62700, 8, "mem_62700"))
            return 1;
        for (int32_t i_60737 = 0; i_60737 < 2; i_60737++) {
            for (int32_t i_64205 = 0; i_64205 < 2; i_64205++) {
                *(int32_t *) &mem_62700.mem[i_64205 * 4] = i_60737;
            }
            memmove(mem_62695.mem + 2 * i_60737 * 4, mem_62700.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_62700, "mem_62700") != 0)
            return 1;
        
        struct memblock mem_62705;
        
        mem_62705.references = NULL;
        if (memblock_alloc(ctx, &mem_62705, 16, "mem_62705"))
            return 1;
        
        struct memblock mem_62708;
        
        mem_62708.references = NULL;
        if (memblock_alloc(ctx, &mem_62708, 16, "mem_62708"))
            return 1;
        
        int32_t discard_60747;
        int32_t scanacc_60741 = 0;
        
        for (int32_t i_60744 = 0; i_60744 < 4; i_60744++) {
            bool not_arg_58099 = i_60744 == 0;
            bool res_58100 = !not_arg_58099;
            int32_t part_res_58101;
            
            if (res_58100) {
                part_res_58101 = 0;
            } else {
                part_res_58101 = 1;
            }
            
            int32_t part_res_58102;
            
            if (res_58100) {
                part_res_58102 = 1;
            } else {
                part_res_58102 = 0;
            }
            
            int32_t zz_58097 = part_res_58102 + scanacc_60741;
            
            *(int32_t *) &mem_62705.mem[i_60744 * 4] = zz_58097;
            *(int32_t *) &mem_62708.mem[i_60744 * 4] = part_res_58101;
            
            int32_t scanacc_tmp_64206 = zz_58097;
            
            scanacc_60741 = scanacc_tmp_64206;
        }
        discard_60747 = scanacc_60741;
        
        int32_t last_offset_58103 = *(int32_t *) &mem_62705.mem[12];
        int64_t binop_x_62718 = sext_i32_i64(last_offset_58103);
        int64_t bytes_62717 = 4 * binop_x_62718;
        struct memblock mem_62719;
        
        mem_62719.references = NULL;
        if (memblock_alloc(ctx, &mem_62719, bytes_62717, "mem_62719"))
            return 1;
        
        struct memblock mem_62722;
        
        mem_62722.references = NULL;
        if (memblock_alloc(ctx, &mem_62722, bytes_62717, "mem_62722"))
            return 1;
        
        struct memblock mem_62725;
        
        mem_62725.references = NULL;
        if (memblock_alloc(ctx, &mem_62725, bytes_62717, "mem_62725"))
            return 1;
        for (int32_t write_iter_60748 = 0; write_iter_60748 < 4;
             write_iter_60748++) {
            int32_t write_iv_60752 =
                    *(int32_t *) &mem_62708.mem[write_iter_60748 * 4];
            int32_t write_iv_60753 =
                    *(int32_t *) &mem_62705.mem[write_iter_60748 * 4];
            int32_t new_index_61965 = squot32(write_iter_60748, 2);
            int32_t binop_y_61967 = 2 * new_index_61965;
            int32_t new_index_61968 = write_iter_60748 - binop_y_61967;
            bool is_this_one_58115 = write_iv_60752 == 0;
            int32_t this_offset_58116 = -1 + write_iv_60753;
            int32_t total_res_58117;
            
            if (is_this_one_58115) {
                total_res_58117 = this_offset_58116;
            } else {
                total_res_58117 = -1;
            }
            
            bool less_than_zzero_60757 = slt32(total_res_58117, 0);
            bool greater_than_sizze_60758 = sle32(last_offset_58103,
                                                  total_res_58117);
            bool outside_bounds_dim_60759 = less_than_zzero_60757 ||
                 greater_than_sizze_60758;
            
            if (!outside_bounds_dim_60759) {
                memmove(mem_62719.mem + total_res_58117 * 4, mem_62695.mem +
                        (2 * new_index_61965 + new_index_61968) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_60759) {
                struct memblock mem_62734;
                
                mem_62734.references = NULL;
                if (memblock_alloc(ctx, &mem_62734, 4, "mem_62734"))
                    return 1;
                
                int32_t x_64213;
                
                for (int32_t i_64212 = 0; i_64212 < 1; i_64212++) {
                    x_64213 = new_index_61968 + sext_i32_i32(i_64212);
                    *(int32_t *) &mem_62734.mem[i_64212 * 4] = x_64213;
                }
                memmove(mem_62722.mem + total_res_58117 * 4, mem_62734.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62734, "mem_62734") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62734, "mem_62734") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_60759) {
                struct memblock mem_62737;
                
                mem_62737.references = NULL;
                if (memblock_alloc(ctx, &mem_62737, 4, "mem_62737"))
                    return 1;
                
                int32_t x_64215;
                
                for (int32_t i_64214 = 0; i_64214 < 1; i_64214++) {
                    x_64215 = write_iter_60748 + sext_i32_i32(i_64214);
                    *(int32_t *) &mem_62737.mem[i_64214 * 4] = x_64215;
                }
                memmove(mem_62725.mem + total_res_58117 * 4, mem_62737.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62737, "mem_62737") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62737, "mem_62737") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_62695, "mem_62695") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62705, "mem_62705") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62708, "mem_62708") != 0)
            return 1;
        
        int32_t conc_tmp_58128 = 1 + last_offset_58103;
        int32_t res_58139;
        int32_t redout_60775 = last_offset_58103;
        
        for (int32_t i_60776 = 0; i_60776 < last_offset_58103; i_60776++) {
            int32_t x_58143 = *(int32_t *) &mem_62719.mem[i_60776 * 4];
            int32_t x_58144 = *(int32_t *) &mem_62722.mem[i_60776 * 4];
            bool cond_58146 = x_58143 == 0;
            bool cond_58147 = x_58144 == 0;
            bool eq_58148 = cond_58146 && cond_58147;
            int32_t res_58149;
            
            if (eq_58148) {
                res_58149 = i_60776;
            } else {
                res_58149 = last_offset_58103;
            }
            
            int32_t res_58142 = smin32(res_58149, redout_60775);
            int32_t redout_tmp_64216 = res_58142;
            
            redout_60775 = redout_tmp_64216;
        }
        res_58139 = redout_60775;
        
        bool cond_58150 = res_58139 == last_offset_58103;
        int32_t res_58151;
        
        if (cond_58150) {
            res_58151 = -1;
        } else {
            res_58151 = res_58139;
        }
        
        bool eq_x_zz_58152 = -1 == res_58139;
        bool not_p_58153 = !cond_58150;
        bool p_and_eq_x_y_58154 = eq_x_zz_58152 && not_p_58153;
        bool cond_58155 = cond_58150 || p_and_eq_x_y_58154;
        bool cond_58156 = !cond_58155;
        int32_t sizze_58157;
        
        if (cond_58156) {
            sizze_58157 = last_offset_58103;
        } else {
            sizze_58157 = conc_tmp_58128;
        }
        
        int64_t binop_x_62748 = sext_i32_i64(conc_tmp_58128);
        int64_t bytes_62747 = 4 * binop_x_62748;
        int64_t res_mem_sizze_62756;
        struct memblock res_mem_62757;
        
        res_mem_62757.references = NULL;
        
        int64_t res_mem_sizze_62758;
        struct memblock res_mem_62759;
        
        res_mem_62759.references = NULL;
        
        int64_t res_mem_sizze_62760;
        struct memblock res_mem_62761;
        
        res_mem_62761.references = NULL;
        if (cond_58156) {
            struct memblock mem_62746;
            
            mem_62746.references = NULL;
            if (memblock_alloc(ctx, &mem_62746, bytes_62717, "mem_62746"))
                return 1;
            memmove(mem_62746.mem + 0, mem_62725.mem + 0, last_offset_58103 *
                    sizeof(int32_t));
            
            bool less_than_zzero_60779 = slt32(res_58151, 0);
            bool greater_than_sizze_60780 = sle32(last_offset_58103, res_58151);
            bool outside_bounds_dim_60781 = less_than_zzero_60779 ||
                 greater_than_sizze_60780;
            
            if (!outside_bounds_dim_60781) {
                *(int32_t *) &mem_62746.mem[res_58151 * 4] = 4;
            }
            res_mem_sizze_62756 = bytes_62717;
            if (memblock_set(ctx, &res_mem_62757, &mem_62719, "mem_62719") != 0)
                return 1;
            res_mem_sizze_62758 = bytes_62717;
            if (memblock_set(ctx, &res_mem_62759, &mem_62722, "mem_62722") != 0)
                return 1;
            res_mem_sizze_62760 = bytes_62717;
            if (memblock_set(ctx, &res_mem_62761, &mem_62746, "mem_62746") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62746, "mem_62746") != 0)
                return 1;
        } else {
            struct memblock mem_62749;
            
            mem_62749.references = NULL;
            if (memblock_alloc(ctx, &mem_62749, bytes_62747, "mem_62749"))
                return 1;
            
            int32_t tmp_offs_64217 = 0;
            
            memmove(mem_62749.mem + tmp_offs_64217 * 4, mem_62719.mem + 0,
                    last_offset_58103 * sizeof(int32_t));
            tmp_offs_64217 += last_offset_58103;
            memmove(mem_62749.mem + tmp_offs_64217 * 4, mem_62662.mem + 0,
                    sizeof(int32_t));
            tmp_offs_64217 += 1;
            
            struct memblock mem_62752;
            
            mem_62752.references = NULL;
            if (memblock_alloc(ctx, &mem_62752, bytes_62747, "mem_62752"))
                return 1;
            
            int32_t tmp_offs_64218 = 0;
            
            memmove(mem_62752.mem + tmp_offs_64218 * 4, mem_62722.mem + 0,
                    last_offset_58103 * sizeof(int32_t));
            tmp_offs_64218 += last_offset_58103;
            memmove(mem_62752.mem + tmp_offs_64218 * 4, mem_62662.mem + 0,
                    sizeof(int32_t));
            tmp_offs_64218 += 1;
            
            struct memblock mem_62755;
            
            mem_62755.references = NULL;
            if (memblock_alloc(ctx, &mem_62755, bytes_62747, "mem_62755"))
                return 1;
            
            int32_t tmp_offs_64219 = 0;
            
            memmove(mem_62755.mem + tmp_offs_64219 * 4, mem_62725.mem + 0,
                    last_offset_58103 * sizeof(int32_t));
            tmp_offs_64219 += last_offset_58103;
            memmove(mem_62755.mem + tmp_offs_64219 * 4, mem_62665.mem + 0,
                    sizeof(int32_t));
            tmp_offs_64219 += 1;
            res_mem_sizze_62756 = bytes_62747;
            if (memblock_set(ctx, &res_mem_62757, &mem_62749, "mem_62749") != 0)
                return 1;
            res_mem_sizze_62758 = bytes_62747;
            if (memblock_set(ctx, &res_mem_62759, &mem_62752, "mem_62752") != 0)
                return 1;
            res_mem_sizze_62760 = bytes_62747;
            if (memblock_set(ctx, &res_mem_62761, &mem_62755, "mem_62755") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62755, "mem_62755") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62752, "mem_62752") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62749, "mem_62749") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_62719, "mem_62719") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62722, "mem_62722") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62725, "mem_62725") != 0)
            return 1;
        
        bool eq_x_y_58172 = 4 == last_offset_58103;
        bool eq_x_zz_58173 = 4 == conc_tmp_58128;
        bool p_and_eq_x_y_58174 = cond_58156 && eq_x_y_58172;
        bool p_and_eq_x_y_58175 = cond_58155 && eq_x_zz_58173;
        bool dim_eq_58176 = p_and_eq_x_y_58174 || p_and_eq_x_y_58175;
        bool arrays_equal_58177;
        
        if (dim_eq_58176) {
            bool all_equal_58179;
            bool redout_60785 = 1;
            
            for (int32_t i_60786 = 0; i_60786 < sizze_58157; i_60786++) {
                int32_t x_58183 = *(int32_t *) &res_mem_62761.mem[i_60786 * 4];
                int32_t y_58184 = *(int32_t *) &mem_62430.mem[i_60786 * 4];
                bool res_58185 = x_58183 == y_58184;
                bool res_58182 = res_58185 && redout_60785;
                bool redout_tmp_64220 = res_58182;
                
                redout_60785 = redout_tmp_64220;
            }
            all_equal_58179 = redout_60785;
            arrays_equal_58177 = all_equal_58179;
        } else {
            arrays_equal_58177 = 0;
        }
        if (memblock_unref(ctx, &res_mem_62761, "res_mem_62761") != 0)
            return 1;
        
        bool res_58186;
        
        if (arrays_equal_58177) {
            bool arrays_equal_58187;
            
            if (dim_eq_58176) {
                bool all_equal_58189;
                bool redout_60787 = 1;
                
                for (int32_t i_60788 = 0; i_60788 < sizze_58157; i_60788++) {
                    int32_t x_58193 = *(int32_t *) &res_mem_62757.mem[i_60788 *
                                                                      4];
                    int32_t y_58194 = *(int32_t *) &mem_62689.mem[i_60788 * 4];
                    bool res_58195 = x_58193 == y_58194;
                    bool res_58192 = res_58195 && redout_60787;
                    bool redout_tmp_64221 = res_58192;
                    
                    redout_60787 = redout_tmp_64221;
                }
                all_equal_58189 = redout_60787;
                arrays_equal_58187 = all_equal_58189;
            } else {
                arrays_equal_58187 = 0;
            }
            
            bool arrays_equal_58196;
            
            if (dim_eq_58176) {
                bool all_equal_58198;
                bool redout_60789 = 1;
                
                for (int32_t i_60790 = 0; i_60790 < sizze_58157; i_60790++) {
                    int32_t x_58202 = *(int32_t *) &res_mem_62759.mem[i_60790 *
                                                                      4];
                    int32_t y_58203 = *(int32_t *) &mem_62692.mem[i_60790 * 4];
                    bool res_58204 = x_58202 == y_58203;
                    bool res_58201 = res_58204 && redout_60789;
                    bool redout_tmp_64222 = res_58201;
                    
                    redout_60789 = redout_tmp_64222;
                }
                all_equal_58198 = redout_60789;
                arrays_equal_58196 = all_equal_58198;
            } else {
                arrays_equal_58196 = 0;
            }
            
            bool eq_58205 = arrays_equal_58187 && arrays_equal_58196;
            
            res_58186 = eq_58205;
        } else {
            res_58186 = 0;
        }
        if (memblock_unref(ctx, &res_mem_62757, "res_mem_62757") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62759, "res_mem_62759") != 0)
            return 1;
        cond_58088 = res_58186;
        if (memblock_unref(ctx, &res_mem_62761, "res_mem_62761") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62759, "res_mem_62759") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62757, "res_mem_62757") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62725, "mem_62725") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62722, "mem_62722") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62719, "mem_62719") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62708, "mem_62708") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62705, "mem_62705") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62700, "mem_62700") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62695, "mem_62695") != 0)
            return 1;
    } else {
        cond_58088 = 0;
    }
    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
        return 1;
    
    bool res_58206;
    
    if (cond_58088) {
        struct memblock mem_62764;
        
        mem_62764.references = NULL;
        if (memblock_alloc(ctx, &mem_62764, 16, "mem_62764"))
            return 1;
        
        struct memblock mem_62769;
        
        mem_62769.references = NULL;
        if (memblock_alloc(ctx, &mem_62769, 8, "mem_62769"))
            return 1;
        for (int32_t i_60793 = 0; i_60793 < 2; i_60793++) {
            for (int32_t i_64224 = 0; i_64224 < 2; i_64224++) {
                *(int32_t *) &mem_62769.mem[i_64224 * 4] = i_60793;
            }
            memmove(mem_62764.mem + 2 * i_60793 * 4, mem_62769.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_62769, "mem_62769") != 0)
            return 1;
        
        struct memblock mem_62774;
        
        mem_62774.references = NULL;
        if (memblock_alloc(ctx, &mem_62774, 16, "mem_62774"))
            return 1;
        
        struct memblock mem_62777;
        
        mem_62777.references = NULL;
        if (memblock_alloc(ctx, &mem_62777, 16, "mem_62777"))
            return 1;
        
        int32_t discard_60803;
        int32_t scanacc_60797 = 0;
        
        for (int32_t i_60800 = 0; i_60800 < 4; i_60800++) {
            bool not_arg_58217 = i_60800 == 0;
            bool res_58218 = !not_arg_58217;
            int32_t part_res_58219;
            
            if (res_58218) {
                part_res_58219 = 0;
            } else {
                part_res_58219 = 1;
            }
            
            int32_t part_res_58220;
            
            if (res_58218) {
                part_res_58220 = 1;
            } else {
                part_res_58220 = 0;
            }
            
            int32_t zz_58215 = part_res_58220 + scanacc_60797;
            
            *(int32_t *) &mem_62774.mem[i_60800 * 4] = zz_58215;
            *(int32_t *) &mem_62777.mem[i_60800 * 4] = part_res_58219;
            
            int32_t scanacc_tmp_64225 = zz_58215;
            
            scanacc_60797 = scanacc_tmp_64225;
        }
        discard_60803 = scanacc_60797;
        
        int32_t last_offset_58221 = *(int32_t *) &mem_62774.mem[12];
        int64_t binop_x_62787 = sext_i32_i64(last_offset_58221);
        int64_t bytes_62786 = 4 * binop_x_62787;
        struct memblock mem_62788;
        
        mem_62788.references = NULL;
        if (memblock_alloc(ctx, &mem_62788, bytes_62786, "mem_62788"))
            return 1;
        
        struct memblock mem_62791;
        
        mem_62791.references = NULL;
        if (memblock_alloc(ctx, &mem_62791, bytes_62786, "mem_62791"))
            return 1;
        
        struct memblock mem_62794;
        
        mem_62794.references = NULL;
        if (memblock_alloc(ctx, &mem_62794, bytes_62786, "mem_62794"))
            return 1;
        for (int32_t write_iter_60804 = 0; write_iter_60804 < 4;
             write_iter_60804++) {
            int32_t write_iv_60808 =
                    *(int32_t *) &mem_62777.mem[write_iter_60804 * 4];
            int32_t write_iv_60809 =
                    *(int32_t *) &mem_62774.mem[write_iter_60804 * 4];
            int32_t new_index_61985 = squot32(write_iter_60804, 2);
            int32_t binop_y_61987 = 2 * new_index_61985;
            int32_t new_index_61988 = write_iter_60804 - binop_y_61987;
            bool is_this_one_58233 = write_iv_60808 == 0;
            int32_t this_offset_58234 = -1 + write_iv_60809;
            int32_t total_res_58235;
            
            if (is_this_one_58233) {
                total_res_58235 = this_offset_58234;
            } else {
                total_res_58235 = -1;
            }
            
            bool less_than_zzero_60813 = slt32(total_res_58235, 0);
            bool greater_than_sizze_60814 = sle32(last_offset_58221,
                                                  total_res_58235);
            bool outside_bounds_dim_60815 = less_than_zzero_60813 ||
                 greater_than_sizze_60814;
            
            if (!outside_bounds_dim_60815) {
                memmove(mem_62788.mem + total_res_58235 * 4, mem_62764.mem +
                        (2 * new_index_61985 + new_index_61988) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_60815) {
                struct memblock mem_62803;
                
                mem_62803.references = NULL;
                if (memblock_alloc(ctx, &mem_62803, 4, "mem_62803"))
                    return 1;
                
                int32_t x_64232;
                
                for (int32_t i_64231 = 0; i_64231 < 1; i_64231++) {
                    x_64232 = new_index_61988 + sext_i32_i32(i_64231);
                    *(int32_t *) &mem_62803.mem[i_64231 * 4] = x_64232;
                }
                memmove(mem_62791.mem + total_res_58235 * 4, mem_62803.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62803, "mem_62803") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62803, "mem_62803") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_60815) {
                struct memblock mem_62806;
                
                mem_62806.references = NULL;
                if (memblock_alloc(ctx, &mem_62806, 4, "mem_62806"))
                    return 1;
                
                int32_t x_64234;
                
                for (int32_t i_64233 = 0; i_64233 < 1; i_64233++) {
                    x_64234 = write_iter_60804 + sext_i32_i32(i_64233);
                    *(int32_t *) &mem_62806.mem[i_64233 * 4] = x_64234;
                }
                memmove(mem_62794.mem + total_res_58235 * 4, mem_62806.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62806, "mem_62806") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62806, "mem_62806") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_62764, "mem_62764") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62774, "mem_62774") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62777, "mem_62777") != 0)
            return 1;
        
        int32_t res_58256;
        int32_t redout_60831 = last_offset_58221;
        
        for (int32_t i_60832 = 0; i_60832 < last_offset_58221; i_60832++) {
            int32_t x_58260 = *(int32_t *) &mem_62788.mem[i_60832 * 4];
            int32_t x_58261 = *(int32_t *) &mem_62791.mem[i_60832 * 4];
            bool cond_58263 = x_58260 == 0;
            bool cond_58264 = x_58261 == 1;
            bool eq_58265 = cond_58263 && cond_58264;
            int32_t res_58266;
            
            if (eq_58265) {
                res_58266 = i_60832;
            } else {
                res_58266 = last_offset_58221;
            }
            
            int32_t res_58259 = smin32(res_58266, redout_60831);
            int32_t redout_tmp_64235 = res_58259;
            
            redout_60831 = redout_tmp_64235;
        }
        res_58256 = redout_60831;
        if (memblock_unref(ctx, &mem_62788, "mem_62788") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62791, "mem_62791") != 0)
            return 1;
        
        bool cond_58267 = res_58256 == last_offset_58221;
        int32_t res_58268;
        
        if (cond_58267) {
            res_58268 = -1;
        } else {
            res_58268 = res_58256;
        }
        
        bool eq_x_zz_58269 = -1 == res_58256;
        bool not_p_58270 = !cond_58267;
        bool p_and_eq_x_y_58271 = eq_x_zz_58269 && not_p_58270;
        bool cond_58272 = cond_58267 || p_and_eq_x_y_58271;
        int32_t res_58273;
        
        if (cond_58272) {
            res_58273 = 0;
        } else {
            int32_t res_58274 = *(int32_t *) &mem_62794.mem[res_58268 * 4];
            
            res_58273 = res_58274;
        }
        if (memblock_unref(ctx, &mem_62794, "mem_62794") != 0)
            return 1;
        
        bool res_58275 = res_58273 == 1;
        
        res_58206 = res_58275;
        if (memblock_unref(ctx, &mem_62794, "mem_62794") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62791, "mem_62791") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62788, "mem_62788") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62777, "mem_62777") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62774, "mem_62774") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62769, "mem_62769") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62764, "mem_62764") != 0)
            return 1;
    } else {
        res_58206 = 0;
    }
    
    struct memblock mem_62815;
    
    mem_62815.references = NULL;
    if (memblock_alloc(ctx, &mem_62815, 16, "mem_62815"))
        return 1;
    
    struct memblock mem_62820;
    
    mem_62820.references = NULL;
    if (memblock_alloc(ctx, &mem_62820, 8, "mem_62820"))
        return 1;
    for (int32_t i_60835 = 0; i_60835 < 2; i_60835++) {
        for (int32_t i_64237 = 0; i_64237 < 2; i_64237++) {
            *(int32_t *) &mem_62820.mem[i_64237 * 4] = i_60835;
        }
        memmove(mem_62815.mem + 2 * i_60835 * 4, mem_62820.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
        return 1;
    
    struct memblock mem_62825;
    
    mem_62825.references = NULL;
    if (memblock_alloc(ctx, &mem_62825, 16, "mem_62825"))
        return 1;
    
    struct memblock mem_62828;
    
    mem_62828.references = NULL;
    if (memblock_alloc(ctx, &mem_62828, 16, "mem_62828"))
        return 1;
    
    int32_t discard_60845;
    int32_t scanacc_60839 = 0;
    
    for (int32_t i_60842 = 0; i_60842 < 4; i_60842++) {
        bool not_arg_58286 = i_60842 == 0;
        bool res_58287 = !not_arg_58286;
        int32_t part_res_58288;
        
        if (res_58287) {
            part_res_58288 = 0;
        } else {
            part_res_58288 = 1;
        }
        
        int32_t part_res_58289;
        
        if (res_58287) {
            part_res_58289 = 1;
        } else {
            part_res_58289 = 0;
        }
        
        int32_t zz_58284 = part_res_58289 + scanacc_60839;
        
        *(int32_t *) &mem_62825.mem[i_60842 * 4] = zz_58284;
        *(int32_t *) &mem_62828.mem[i_60842 * 4] = part_res_58288;
        
        int32_t scanacc_tmp_64238 = zz_58284;
        
        scanacc_60839 = scanacc_tmp_64238;
    }
    discard_60845 = scanacc_60839;
    
    int32_t last_offset_58290 = *(int32_t *) &mem_62825.mem[12];
    int64_t binop_x_62838 = sext_i32_i64(last_offset_58290);
    int64_t bytes_62837 = 4 * binop_x_62838;
    struct memblock mem_62839;
    
    mem_62839.references = NULL;
    if (memblock_alloc(ctx, &mem_62839, bytes_62837, "mem_62839"))
        return 1;
    
    struct memblock mem_62842;
    
    mem_62842.references = NULL;
    if (memblock_alloc(ctx, &mem_62842, bytes_62837, "mem_62842"))
        return 1;
    
    struct memblock mem_62845;
    
    mem_62845.references = NULL;
    if (memblock_alloc(ctx, &mem_62845, bytes_62837, "mem_62845"))
        return 1;
    for (int32_t write_iter_60846 = 0; write_iter_60846 < 4;
         write_iter_60846++) {
        int32_t write_iv_60850 = *(int32_t *) &mem_62828.mem[write_iter_60846 *
                                                             4];
        int32_t write_iv_60851 = *(int32_t *) &mem_62825.mem[write_iter_60846 *
                                                             4];
        int32_t new_index_62005 = squot32(write_iter_60846, 2);
        int32_t binop_y_62007 = 2 * new_index_62005;
        int32_t new_index_62008 = write_iter_60846 - binop_y_62007;
        bool is_this_one_58302 = write_iv_60850 == 0;
        int32_t this_offset_58303 = -1 + write_iv_60851;
        int32_t total_res_58304;
        
        if (is_this_one_58302) {
            total_res_58304 = this_offset_58303;
        } else {
            total_res_58304 = -1;
        }
        
        bool less_than_zzero_60855 = slt32(total_res_58304, 0);
        bool greater_than_sizze_60856 = sle32(last_offset_58290,
                                              total_res_58304);
        bool outside_bounds_dim_60857 = less_than_zzero_60855 ||
             greater_than_sizze_60856;
        
        if (!outside_bounds_dim_60857) {
            memmove(mem_62839.mem + total_res_58304 * 4, mem_62815.mem + (2 *
                                                                          new_index_62005 +
                                                                          new_index_62008) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_60857) {
            struct memblock mem_62854;
            
            mem_62854.references = NULL;
            if (memblock_alloc(ctx, &mem_62854, 4, "mem_62854"))
                return 1;
            
            int32_t x_64245;
            
            for (int32_t i_64244 = 0; i_64244 < 1; i_64244++) {
                x_64245 = new_index_62008 + sext_i32_i32(i_64244);
                *(int32_t *) &mem_62854.mem[i_64244 * 4] = x_64245;
            }
            memmove(mem_62842.mem + total_res_58304 * 4, mem_62854.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_62854, "mem_62854") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62854, "mem_62854") != 0)
                return 1;
        }
        if (!outside_bounds_dim_60857) {
            struct memblock mem_62857;
            
            mem_62857.references = NULL;
            if (memblock_alloc(ctx, &mem_62857, 4, "mem_62857"))
                return 1;
            
            int32_t x_64247;
            
            for (int32_t i_64246 = 0; i_64246 < 1; i_64246++) {
                x_64247 = write_iter_60846 + sext_i32_i32(i_64246);
                *(int32_t *) &mem_62857.mem[i_64246 * 4] = x_64247;
            }
            memmove(mem_62845.mem + total_res_58304 * 4, mem_62857.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_62857, "mem_62857") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62857, "mem_62857") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
        return 1;
    
    struct memblock mem_62866;
    
    mem_62866.references = NULL;
    if (memblock_alloc(ctx, &mem_62866, 16, "mem_62866"))
        return 1;
    for (int32_t i_64248 = 0; i_64248 < 4; i_64248++) {
        *(int32_t *) &mem_62866.mem[i_64248 * 4] = 0;
    }
    for (int32_t write_iter_60873 = 0; write_iter_60873 < last_offset_58290;
         write_iter_60873++) {
        int32_t write_iv_60875 = *(int32_t *) &mem_62842.mem[write_iter_60873 *
                                                             4];
        int32_t write_iv_60876 = *(int32_t *) &mem_62839.mem[write_iter_60873 *
                                                             4];
        int32_t x_58310 = 2 * write_iv_60875;
        int32_t res_58311 = x_58310 + write_iv_60876;
        bool less_than_zzero_60878 = slt32(res_58311, 0);
        bool greater_than_sizze_60879 = sle32(4, res_58311);
        bool outside_bounds_dim_60880 = less_than_zzero_60878 ||
             greater_than_sizze_60879;
        
        if (!outside_bounds_dim_60880) {
            memmove(mem_62866.mem + res_58311 * 4, mem_62845.mem +
                    write_iter_60873 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
        return 1;
    
    bool res_58312;
    bool redout_60884 = 1;
    
    for (int32_t i_60885 = 0; i_60885 < 2; i_60885++) {
        int32_t binop_x_58317 = 2 * i_60885;
        int32_t new_index_58318 = binop_x_58317 + i_60885;
        int32_t y_58319 = *(int32_t *) &mem_62866.mem[new_index_58318 * 4];
        bool res_58320 = new_index_58318 == y_58319;
        bool x_58315 = res_58320 && redout_60884;
        bool redout_tmp_64250 = x_58315;
        
        redout_60884 = redout_tmp_64250;
    }
    res_58312 = redout_60884;
    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
        return 1;
    
    bool cond_58321;
    
    if (res_58312) {
        struct memblock mem_62873;
        
        mem_62873.references = NULL;
        if (memblock_alloc(ctx, &mem_62873, 16, "mem_62873"))
            return 1;
        
        struct memblock mem_62878;
        
        mem_62878.references = NULL;
        if (memblock_alloc(ctx, &mem_62878, 8, "mem_62878"))
            return 1;
        for (int32_t i_60888 = 0; i_60888 < 2; i_60888++) {
            for (int32_t i_64252 = 0; i_64252 < 2; i_64252++) {
                *(int32_t *) &mem_62878.mem[i_64252 * 4] = i_60888;
            }
            memmove(mem_62873.mem + 2 * i_60888 * 4, mem_62878.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_62878, "mem_62878") != 0)
            return 1;
        
        struct memblock mem_62883;
        
        mem_62883.references = NULL;
        if (memblock_alloc(ctx, &mem_62883, 16, "mem_62883"))
            return 1;
        
        struct memblock mem_62886;
        
        mem_62886.references = NULL;
        if (memblock_alloc(ctx, &mem_62886, 16, "mem_62886"))
            return 1;
        
        int32_t discard_60898;
        int32_t scanacc_60892 = 0;
        
        for (int32_t i_60895 = 0; i_60895 < 4; i_60895++) {
            bool not_arg_58332 = i_60895 == 0;
            bool res_58333 = !not_arg_58332;
            int32_t part_res_58334;
            
            if (res_58333) {
                part_res_58334 = 0;
            } else {
                part_res_58334 = 1;
            }
            
            int32_t part_res_58335;
            
            if (res_58333) {
                part_res_58335 = 1;
            } else {
                part_res_58335 = 0;
            }
            
            int32_t zz_58330 = part_res_58335 + scanacc_60892;
            
            *(int32_t *) &mem_62883.mem[i_60895 * 4] = zz_58330;
            *(int32_t *) &mem_62886.mem[i_60895 * 4] = part_res_58334;
            
            int32_t scanacc_tmp_64253 = zz_58330;
            
            scanacc_60892 = scanacc_tmp_64253;
        }
        discard_60898 = scanacc_60892;
        
        int32_t last_offset_58336 = *(int32_t *) &mem_62883.mem[12];
        int64_t binop_x_62896 = sext_i32_i64(last_offset_58336);
        int64_t bytes_62895 = 4 * binop_x_62896;
        struct memblock mem_62897;
        
        mem_62897.references = NULL;
        if (memblock_alloc(ctx, &mem_62897, bytes_62895, "mem_62897"))
            return 1;
        
        struct memblock mem_62900;
        
        mem_62900.references = NULL;
        if (memblock_alloc(ctx, &mem_62900, bytes_62895, "mem_62900"))
            return 1;
        
        struct memblock mem_62903;
        
        mem_62903.references = NULL;
        if (memblock_alloc(ctx, &mem_62903, bytes_62895, "mem_62903"))
            return 1;
        for (int32_t write_iter_60899 = 0; write_iter_60899 < 4;
             write_iter_60899++) {
            int32_t write_iv_60903 =
                    *(int32_t *) &mem_62886.mem[write_iter_60899 * 4];
            int32_t write_iv_60904 =
                    *(int32_t *) &mem_62883.mem[write_iter_60899 * 4];
            int32_t new_index_62026 = squot32(write_iter_60899, 2);
            int32_t binop_y_62028 = 2 * new_index_62026;
            int32_t new_index_62029 = write_iter_60899 - binop_y_62028;
            bool is_this_one_58348 = write_iv_60903 == 0;
            int32_t this_offset_58349 = -1 + write_iv_60904;
            int32_t total_res_58350;
            
            if (is_this_one_58348) {
                total_res_58350 = this_offset_58349;
            } else {
                total_res_58350 = -1;
            }
            
            bool less_than_zzero_60908 = slt32(total_res_58350, 0);
            bool greater_than_sizze_60909 = sle32(last_offset_58336,
                                                  total_res_58350);
            bool outside_bounds_dim_60910 = less_than_zzero_60908 ||
                 greater_than_sizze_60909;
            
            if (!outside_bounds_dim_60910) {
                memmove(mem_62897.mem + total_res_58350 * 4, mem_62873.mem +
                        (2 * new_index_62026 + new_index_62029) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_60910) {
                struct memblock mem_62912;
                
                mem_62912.references = NULL;
                if (memblock_alloc(ctx, &mem_62912, 4, "mem_62912"))
                    return 1;
                
                int32_t x_64260;
                
                for (int32_t i_64259 = 0; i_64259 < 1; i_64259++) {
                    x_64260 = new_index_62029 + sext_i32_i32(i_64259);
                    *(int32_t *) &mem_62912.mem[i_64259 * 4] = x_64260;
                }
                memmove(mem_62900.mem + total_res_58350 * 4, mem_62912.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62912, "mem_62912") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62912, "mem_62912") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_60910) {
                struct memblock mem_62915;
                
                mem_62915.references = NULL;
                if (memblock_alloc(ctx, &mem_62915, 4, "mem_62915"))
                    return 1;
                
                int32_t x_64262;
                
                for (int32_t i_64261 = 0; i_64261 < 1; i_64261++) {
                    x_64262 = write_iter_60899 + sext_i32_i32(i_64261);
                    *(int32_t *) &mem_62915.mem[i_64261 * 4] = x_64262;
                }
                memmove(mem_62903.mem + total_res_58350 * 4, mem_62915.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_62915, "mem_62915") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62915, "mem_62915") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_62873, "mem_62873") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62883, "mem_62883") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62886, "mem_62886") != 0)
            return 1;
        
        struct memblock mem_62924;
        
        mem_62924.references = NULL;
        if (memblock_alloc(ctx, &mem_62924, 16, "mem_62924"))
            return 1;
        for (int32_t i_64263 = 0; i_64263 < 4; i_64263++) {
            *(int32_t *) &mem_62924.mem[i_64263 * 4] = 0;
        }
        for (int32_t write_iter_60926 = 0; write_iter_60926 < last_offset_58336;
             write_iter_60926++) {
            int32_t write_iv_60928 =
                    *(int32_t *) &mem_62897.mem[write_iter_60926 * 4];
            int32_t write_iv_60929 =
                    *(int32_t *) &mem_62900.mem[write_iter_60926 * 4];
            int32_t x_58356 = 2 * write_iv_60928;
            int32_t res_58357 = x_58356 + write_iv_60929;
            bool less_than_zzero_60931 = slt32(res_58357, 0);
            bool greater_than_sizze_60932 = sle32(4, res_58357);
            bool outside_bounds_dim_60933 = less_than_zzero_60931 ||
                 greater_than_sizze_60932;
            
            if (!outside_bounds_dim_60933) {
                memmove(mem_62924.mem + res_58357 * 4, mem_62903.mem +
                        write_iter_60926 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_62897, "mem_62897") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62900, "mem_62900") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62903, "mem_62903") != 0)
            return 1;
        
        bool all_equal_58358;
        bool redout_60937 = 1;
        
        for (int32_t i_60938 = 0; i_60938 < 4; i_60938++) {
            int32_t x_58362 = *(int32_t *) &mem_62924.mem[i_60938 * 4];
            bool res_58364 = x_58362 == i_60938;
            bool res_58361 = res_58364 && redout_60937;
            bool redout_tmp_64265 = res_58361;
            
            redout_60937 = redout_tmp_64265;
        }
        all_equal_58358 = redout_60937;
        if (memblock_unref(ctx, &mem_62924, "mem_62924") != 0)
            return 1;
        cond_58321 = all_equal_58358;
        if (memblock_unref(ctx, &mem_62924, "mem_62924") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62903, "mem_62903") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62900, "mem_62900") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62897, "mem_62897") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62886, "mem_62886") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62883, "mem_62883") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62878, "mem_62878") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62873, "mem_62873") != 0)
            return 1;
    } else {
        cond_58321 = 0;
    }
    
    bool res_58365;
    
    if (cond_58321) {
        struct memblock mem_62931;
        
        mem_62931.references = NULL;
        if (memblock_alloc(ctx, &mem_62931, 36, "mem_62931"))
            return 1;
        for (int32_t i_64266 = 0; i_64266 < 9; i_64266++) {
            *(int32_t *) &mem_62931.mem[i_64266 * 4] = 0;
        }
        
        struct memblock mem_62934;
        
        mem_62934.references = NULL;
        if (memblock_alloc(ctx, &mem_62934, 36, "mem_62934"))
            return 1;
        for (int32_t i_64267 = 0; i_64267 < 9; i_64267++) {
            *(int32_t *) &mem_62934.mem[i_64267 * 4] = 0;
        }
        for (int32_t write_iter_60939 = 0; write_iter_60939 < 3;
             write_iter_60939++) {
            int32_t x_58372 = 3 * write_iter_60939;
            int32_t res_58373 = x_58372 + write_iter_60939;
            bool less_than_zzero_60943 = slt32(res_58373, 0);
            bool greater_than_sizze_60944 = sle32(9, res_58373);
            bool outside_bounds_dim_60945 = less_than_zzero_60943 ||
                 greater_than_sizze_60944;
            
            if (!outside_bounds_dim_60945) {
                *(int32_t *) &mem_62934.mem[res_58373 * 4] = 1;
            }
            if (!outside_bounds_dim_60945) {
                *(int32_t *) &mem_62931.mem[res_58373 * 4] = 1;
            }
        }
        
        bool all_equal_58374;
        bool redout_60955 = 1;
        
        for (int32_t i_60956 = 0; i_60956 < 9; i_60956++) {
            int32_t x_58378 = *(int32_t *) &mem_62934.mem[i_60956 * 4];
            int32_t y_58379 = *(int32_t *) &mem_62931.mem[i_60956 * 4];
            bool res_58380 = x_58378 == y_58379;
            bool res_58377 = res_58380 && redout_60955;
            bool redout_tmp_64270 = res_58377;
            
            redout_60955 = redout_tmp_64270;
        }
        all_equal_58374 = redout_60955;
        if (memblock_unref(ctx, &mem_62931, "mem_62931") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62934, "mem_62934") != 0)
            return 1;
        res_58365 = all_equal_58374;
        if (memblock_unref(ctx, &mem_62934, "mem_62934") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62931, "mem_62931") != 0)
            return 1;
    } else {
        res_58365 = 0;
    }
    
    struct memblock mem_62945;
    
    mem_62945.references = NULL;
    if (memblock_alloc(ctx, &mem_62945, 16, "mem_62945"))
        return 1;
    
    struct memblock mem_62950;
    
    mem_62950.references = NULL;
    if (memblock_alloc(ctx, &mem_62950, 8, "mem_62950"))
        return 1;
    for (int32_t i_60964 = 0; i_60964 < 2; i_60964++) {
        for (int32_t i_64272 = 0; i_64272 < 2; i_64272++) {
            *(int32_t *) &mem_62950.mem[i_64272 * 4] = i_60964;
        }
        memmove(mem_62945.mem + 2 * i_60964 * 4, mem_62950.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
        return 1;
    
    struct memblock mem_62955;
    
    mem_62955.references = NULL;
    if (memblock_alloc(ctx, &mem_62955, 16, "mem_62955"))
        return 1;
    
    int32_t discard_60971;
    int32_t scanacc_60967 = 0;
    
    for (int32_t i_60969 = 0; i_60969 < 4; i_60969++) {
        int32_t zz_58407 = 1 + scanacc_60967;
        
        *(int32_t *) &mem_62955.mem[i_60969 * 4] = zz_58407;
        
        int32_t scanacc_tmp_64273 = zz_58407;
        
        scanacc_60967 = scanacc_tmp_64273;
    }
    discard_60971 = scanacc_60967;
    
    int32_t last_offset_58409 = *(int32_t *) &mem_62955.mem[12];
    int64_t binop_x_62961 = sext_i32_i64(last_offset_58409);
    int64_t bytes_62960 = 4 * binop_x_62961;
    struct memblock mem_62962;
    
    mem_62962.references = NULL;
    if (memblock_alloc(ctx, &mem_62962, bytes_62960, "mem_62962"))
        return 1;
    
    struct memblock mem_62965;
    
    mem_62965.references = NULL;
    if (memblock_alloc(ctx, &mem_62965, bytes_62960, "mem_62965"))
        return 1;
    
    struct memblock mem_62968;
    
    mem_62968.references = NULL;
    if (memblock_alloc(ctx, &mem_62968, bytes_62960, "mem_62968"))
        return 1;
    for (int32_t write_iter_60972 = 0; write_iter_60972 < 4;
         write_iter_60972++) {
        int32_t write_iv_60976 = *(int32_t *) &mem_62955.mem[write_iter_60972 *
                                                             4];
        int32_t new_index_62047 = squot32(write_iter_60972, 2);
        int32_t binop_y_62049 = 2 * new_index_62047;
        int32_t new_index_62050 = write_iter_60972 - binop_y_62049;
        int32_t this_offset_58419 = -1 + write_iv_60976;
        bool less_than_zzero_60979 = slt32(this_offset_58419, 0);
        bool greater_than_sizze_60980 = sle32(last_offset_58409,
                                              this_offset_58419);
        bool outside_bounds_dim_60981 = less_than_zzero_60979 ||
             greater_than_sizze_60980;
        
        if (!outside_bounds_dim_60981) {
            memmove(mem_62962.mem + this_offset_58419 * 4, mem_62945.mem + (2 *
                                                                            new_index_62047 +
                                                                            new_index_62050) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_60981) {
            struct memblock mem_62977;
            
            mem_62977.references = NULL;
            if (memblock_alloc(ctx, &mem_62977, 4, "mem_62977"))
                return 1;
            
            int32_t x_64279;
            
            for (int32_t i_64278 = 0; i_64278 < 1; i_64278++) {
                x_64279 = new_index_62050 + sext_i32_i32(i_64278);
                *(int32_t *) &mem_62977.mem[i_64278 * 4] = x_64279;
            }
            memmove(mem_62965.mem + this_offset_58419 * 4, mem_62977.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_62977, "mem_62977") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62977, "mem_62977") != 0)
                return 1;
        }
        if (!outside_bounds_dim_60981) {
            *(int32_t *) &mem_62968.mem[this_offset_58419 * 4] = 1;
        }
    }
    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
        return 1;
    
    struct memblock mem_62986;
    
    mem_62986.references = NULL;
    if (memblock_alloc(ctx, &mem_62986, 16, "mem_62986"))
        return 1;
    for (int32_t i_64280 = 0; i_64280 < 4; i_64280++) {
        *(int32_t *) &mem_62986.mem[i_64280 * 4] = 0;
    }
    for (int32_t write_iter_60997 = 0; write_iter_60997 < last_offset_58409;
         write_iter_60997++) {
        int32_t write_iv_60999 = *(int32_t *) &mem_62968.mem[write_iter_60997 *
                                                             4];
        int32_t write_iv_61000 = *(int32_t *) &mem_62962.mem[write_iter_60997 *
                                                             4];
        int32_t write_iv_61001 = *(int32_t *) &mem_62965.mem[write_iter_60997 *
                                                             4];
        int32_t res_58425 = 2 * write_iv_60999;
        int32_t x_58426 = 2 * write_iv_61000;
        int32_t res_58427 = x_58426 + write_iv_61001;
        bool less_than_zzero_61002 = slt32(res_58427, 0);
        bool greater_than_sizze_61003 = sle32(4, res_58427);
        bool outside_bounds_dim_61004 = less_than_zzero_61002 ||
             greater_than_sizze_61003;
        
        if (!outside_bounds_dim_61004) {
            *(int32_t *) &mem_62986.mem[res_58427 * 4] = res_58425;
        }
    }
    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
        return 1;
    
    bool res_58428;
    bool redout_61008 = 1;
    
    for (int32_t i_61009 = 0; i_61009 < 4; i_61009++) {
        int32_t x_58432 = *(int32_t *) &mem_62986.mem[i_61009 * 4];
        bool res_58433 = x_58432 == 2;
        bool x_58431 = res_58433 && redout_61008;
        bool redout_tmp_64282 = x_58431;
        
        redout_61008 = redout_tmp_64282;
    }
    res_58428 = redout_61008;
    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
        return 1;
    
    bool res_58434;
    
    if (res_58428) {
        struct memblock mem_62993;
        
        mem_62993.references = NULL;
        if (memblock_alloc(ctx, &mem_62993, 16, "mem_62993"))
            return 1;
        
        struct memblock mem_62996;
        
        mem_62996.references = NULL;
        if (memblock_alloc(ctx, &mem_62996, 16, "mem_62996"))
            return 1;
        
        int32_t discard_61022;
        int32_t scanacc_61016 = 0;
        
        for (int32_t i_61019 = 0; i_61019 < 4; i_61019++) {
            bool not_arg_58445 = i_61019 == 0;
            bool res_58446 = !not_arg_58445;
            int32_t part_res_58447;
            
            if (res_58446) {
                part_res_58447 = 0;
            } else {
                part_res_58447 = 1;
            }
            
            int32_t part_res_58448;
            
            if (res_58446) {
                part_res_58448 = 1;
            } else {
                part_res_58448 = 0;
            }
            
            int32_t zz_58443 = part_res_58448 + scanacc_61016;
            
            *(int32_t *) &mem_62993.mem[i_61019 * 4] = zz_58443;
            *(int32_t *) &mem_62996.mem[i_61019 * 4] = part_res_58447;
            
            int32_t scanacc_tmp_64283 = zz_58443;
            
            scanacc_61016 = scanacc_tmp_64283;
        }
        discard_61022 = scanacc_61016;
        
        int32_t last_offset_58449 = *(int32_t *) &mem_62993.mem[12];
        int64_t binop_x_63006 = sext_i32_i64(last_offset_58449);
        int64_t bytes_63005 = 4 * binop_x_63006;
        struct memblock mem_63007;
        
        mem_63007.references = NULL;
        if (memblock_alloc(ctx, &mem_63007, bytes_63005, "mem_63007"))
            return 1;
        for (int32_t write_iter_61023 = 0; write_iter_61023 < 4;
             write_iter_61023++) {
            int32_t write_iv_61025 =
                    *(int32_t *) &mem_62996.mem[write_iter_61023 * 4];
            int32_t write_iv_61026 =
                    *(int32_t *) &mem_62993.mem[write_iter_61023 * 4];
            bool is_this_one_58457 = write_iv_61025 == 0;
            int32_t this_offset_58458 = -1 + write_iv_61026;
            int32_t total_res_58459;
            
            if (is_this_one_58457) {
                total_res_58459 = this_offset_58458;
            } else {
                total_res_58459 = -1;
            }
            
            bool less_than_zzero_61030 = slt32(total_res_58459, 0);
            bool greater_than_sizze_61031 = sle32(last_offset_58449,
                                                  total_res_58459);
            bool outside_bounds_dim_61032 = less_than_zzero_61030 ||
                 greater_than_sizze_61031;
            
            if (!outside_bounds_dim_61032) {
                struct memblock mem_63012;
                
                mem_63012.references = NULL;
                if (memblock_alloc(ctx, &mem_63012, 4, "mem_63012"))
                    return 1;
                
                int32_t x_64288;
                
                for (int32_t i_64287 = 0; i_64287 < 1; i_64287++) {
                    x_64288 = write_iter_61023 + sext_i32_i32(i_64287);
                    *(int32_t *) &mem_63012.mem[i_64287 * 4] = x_64288;
                }
                memmove(mem_63007.mem + total_res_58459 * 4, mem_63012.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63012, "mem_63012") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63012, "mem_63012") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_62993, "mem_62993") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62996, "mem_62996") != 0)
            return 1;
        
        bool dim_eq_58461 = 3 == last_offset_58449;
        bool arrays_equal_58462;
        
        if (dim_eq_58461) {
            bool all_equal_58464;
            bool redout_61036 = 1;
            
            for (int32_t i_61037 = 0; i_61037 < 3; i_61037++) {
                int32_t x_58468 = *(int32_t *) &mem_63007.mem[i_61037 * 4];
                int32_t res_58470 = 2 * x_58468;
                int32_t res_58471 = 1 + i_61037;
                int32_t res_58472 = 2 * res_58471;
                bool res_58473 = res_58472 == res_58470;
                bool res_58467 = res_58473 && redout_61036;
                bool redout_tmp_64289 = res_58467;
                
                redout_61036 = redout_tmp_64289;
            }
            all_equal_58464 = redout_61036;
            arrays_equal_58462 = all_equal_58464;
        } else {
            arrays_equal_58462 = 0;
        }
        if (memblock_unref(ctx, &mem_63007, "mem_63007") != 0)
            return 1;
        res_58434 = arrays_equal_58462;
        if (memblock_unref(ctx, &mem_63007, "mem_63007") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62996, "mem_62996") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62993, "mem_62993") != 0)
            return 1;
    } else {
        res_58434 = 0;
    }
    
    struct memblock mem_63017;
    
    mem_63017.references = NULL;
    if (memblock_alloc(ctx, &mem_63017, 24, "mem_63017"))
        return 1;
    
    struct memblock mem_63022;
    
    mem_63022.references = NULL;
    if (memblock_alloc(ctx, &mem_63022, 8, "mem_63022"))
        return 1;
    for (int32_t i_61040 = 0; i_61040 < 3; i_61040++) {
        for (int32_t i_64291 = 0; i_64291 < 2; i_64291++) {
            *(int32_t *) &mem_63022.mem[i_64291 * 4] = i_61040;
        }
        memmove(mem_63017.mem + 2 * i_61040 * 4, mem_63022.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
        return 1;
    
    struct memblock mem_63027;
    
    mem_63027.references = NULL;
    if (memblock_alloc(ctx, &mem_63027, 24, "mem_63027"))
        return 1;
    
    struct memblock mem_63030;
    
    mem_63030.references = NULL;
    if (memblock_alloc(ctx, &mem_63030, 24, "mem_63030"))
        return 1;
    
    int32_t discard_61050;
    int32_t scanacc_61044 = 0;
    
    for (int32_t i_61047 = 0; i_61047 < 6; i_61047++) {
        bool not_arg_58488 = i_61047 == 0;
        bool res_58489 = !not_arg_58488;
        int32_t part_res_58490;
        
        if (res_58489) {
            part_res_58490 = 0;
        } else {
            part_res_58490 = 1;
        }
        
        int32_t part_res_58491;
        
        if (res_58489) {
            part_res_58491 = 1;
        } else {
            part_res_58491 = 0;
        }
        
        int32_t zz_58486 = part_res_58491 + scanacc_61044;
        
        *(int32_t *) &mem_63027.mem[i_61047 * 4] = zz_58486;
        *(int32_t *) &mem_63030.mem[i_61047 * 4] = part_res_58490;
        
        int32_t scanacc_tmp_64292 = zz_58486;
        
        scanacc_61044 = scanacc_tmp_64292;
    }
    discard_61050 = scanacc_61044;
    
    int32_t last_offset_58492 = *(int32_t *) &mem_63027.mem[20];
    int64_t binop_x_63040 = sext_i32_i64(last_offset_58492);
    int64_t bytes_63039 = 4 * binop_x_63040;
    struct memblock mem_63041;
    
    mem_63041.references = NULL;
    if (memblock_alloc(ctx, &mem_63041, bytes_63039, "mem_63041"))
        return 1;
    
    struct memblock mem_63044;
    
    mem_63044.references = NULL;
    if (memblock_alloc(ctx, &mem_63044, bytes_63039, "mem_63044"))
        return 1;
    
    struct memblock mem_63047;
    
    mem_63047.references = NULL;
    if (memblock_alloc(ctx, &mem_63047, bytes_63039, "mem_63047"))
        return 1;
    for (int32_t write_iter_61051 = 0; write_iter_61051 < 6;
         write_iter_61051++) {
        int32_t write_iv_61055 = *(int32_t *) &mem_63030.mem[write_iter_61051 *
                                                             4];
        int32_t write_iv_61056 = *(int32_t *) &mem_63027.mem[write_iter_61051 *
                                                             4];
        int32_t new_index_62077 = squot32(write_iter_61051, 2);
        int32_t binop_y_62079 = 2 * new_index_62077;
        int32_t new_index_62080 = write_iter_61051 - binop_y_62079;
        bool is_this_one_58504 = write_iv_61055 == 0;
        int32_t this_offset_58505 = -1 + write_iv_61056;
        int32_t total_res_58506;
        
        if (is_this_one_58504) {
            total_res_58506 = this_offset_58505;
        } else {
            total_res_58506 = -1;
        }
        
        bool less_than_zzero_61060 = slt32(total_res_58506, 0);
        bool greater_than_sizze_61061 = sle32(last_offset_58492,
                                              total_res_58506);
        bool outside_bounds_dim_61062 = less_than_zzero_61060 ||
             greater_than_sizze_61061;
        
        if (!outside_bounds_dim_61062) {
            memmove(mem_63041.mem + total_res_58506 * 4, mem_63017.mem + (2 *
                                                                          new_index_62077 +
                                                                          new_index_62080) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_61062) {
            struct memblock mem_63056;
            
            mem_63056.references = NULL;
            if (memblock_alloc(ctx, &mem_63056, 4, "mem_63056"))
                return 1;
            
            int32_t x_64299;
            
            for (int32_t i_64298 = 0; i_64298 < 1; i_64298++) {
                x_64299 = new_index_62080 + sext_i32_i32(i_64298);
                *(int32_t *) &mem_63056.mem[i_64298 * 4] = x_64299;
            }
            memmove(mem_63044.mem + total_res_58506 * 4, mem_63056.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_63056, "mem_63056") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63056, "mem_63056") != 0)
                return 1;
        }
        if (!outside_bounds_dim_61062) {
            struct memblock mem_63059;
            
            mem_63059.references = NULL;
            if (memblock_alloc(ctx, &mem_63059, 4, "mem_63059"))
                return 1;
            
            int32_t x_64301;
            
            for (int32_t i_64300 = 0; i_64300 < 1; i_64300++) {
                x_64301 = write_iter_61051 + sext_i32_i32(i_64300);
                *(int32_t *) &mem_63059.mem[i_64300 * 4] = x_64301;
            }
            memmove(mem_63047.mem + total_res_58506 * 4, mem_63059.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_63059, "mem_63059") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63059, "mem_63059") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
        return 1;
    
    bool empty_slice_58507 = last_offset_58492 == 0;
    int32_t m_58508 = last_offset_58492 - 1;
    bool zzero_leq_i_p_m_t_s_58509 = sle32(0, m_58508);
    struct memblock mem_63068;
    
    mem_63068.references = NULL;
    if (memblock_alloc(ctx, &mem_63068, bytes_63039, "mem_63068"))
        return 1;
    
    int32_t tmp_offs_64302 = 0;
    
    memmove(mem_63068.mem + tmp_offs_64302 * 4, mem_62605.mem + 0, 0);
    tmp_offs_64302 = tmp_offs_64302;
    memmove(mem_63068.mem + tmp_offs_64302 * 4, mem_63041.mem + 0,
            last_offset_58492 * sizeof(int32_t));
    tmp_offs_64302 += last_offset_58492;
    
    struct memblock mem_63071;
    
    mem_63071.references = NULL;
    if (memblock_alloc(ctx, &mem_63071, bytes_63039, "mem_63071"))
        return 1;
    
    int32_t tmp_offs_64303 = 0;
    
    memmove(mem_63071.mem + tmp_offs_64303 * 4, mem_62605.mem + 0, 0);
    tmp_offs_64303 = tmp_offs_64303;
    memmove(mem_63071.mem + tmp_offs_64303 * 4, mem_63044.mem + 0,
            last_offset_58492 * sizeof(int32_t));
    tmp_offs_64303 += last_offset_58492;
    
    struct memblock mem_63074;
    
    mem_63074.references = NULL;
    if (memblock_alloc(ctx, &mem_63074, bytes_63039, "mem_63074"))
        return 1;
    
    int32_t tmp_offs_64304 = 0;
    
    memmove(mem_63074.mem + tmp_offs_64304 * 4, mem_62605.mem + 0, 0);
    tmp_offs_64304 = tmp_offs_64304;
    memmove(mem_63074.mem + tmp_offs_64304 * 4, mem_63047.mem + 0,
            last_offset_58492 * sizeof(int32_t));
    tmp_offs_64304 += last_offset_58492;
    
    bool loop_cond_58519 = slt32(1, last_offset_58492);
    int32_t sizze_58520;
    int32_t sizze_58521;
    int32_t sizze_58522;
    int64_t res_mem_sizze_63117;
    struct memblock res_mem_63118;
    
    res_mem_63118.references = NULL;
    
    int64_t res_mem_sizze_63119;
    struct memblock res_mem_63120;
    
    res_mem_63120.references = NULL;
    
    int64_t res_mem_sizze_63121;
    struct memblock res_mem_63122;
    
    res_mem_63122.references = NULL;
    
    int32_t res_58526;
    
    if (empty_slice_58507) {
        struct memblock mem_63077;
        
        mem_63077.references = NULL;
        if (memblock_alloc(ctx, &mem_63077, bytes_63039, "mem_63077"))
            return 1;
        memmove(mem_63077.mem + 0, mem_63068.mem + 0, last_offset_58492 *
                sizeof(int32_t));
        
        struct memblock mem_63080;
        
        mem_63080.references = NULL;
        if (memblock_alloc(ctx, &mem_63080, bytes_63039, "mem_63080"))
            return 1;
        memmove(mem_63080.mem + 0, mem_63071.mem + 0, last_offset_58492 *
                sizeof(int32_t));
        
        struct memblock mem_63083;
        
        mem_63083.references = NULL;
        if (memblock_alloc(ctx, &mem_63083, bytes_63039, "mem_63083"))
            return 1;
        memmove(mem_63083.mem + 0, mem_63074.mem + 0, last_offset_58492 *
                sizeof(int32_t));
        sizze_58520 = last_offset_58492;
        sizze_58521 = last_offset_58492;
        sizze_58522 = last_offset_58492;
        res_mem_sizze_63117 = bytes_63039;
        if (memblock_set(ctx, &res_mem_63118, &mem_63077, "mem_63077") != 0)
            return 1;
        res_mem_sizze_63119 = bytes_63039;
        if (memblock_set(ctx, &res_mem_63120, &mem_63080, "mem_63080") != 0)
            return 1;
        res_mem_sizze_63121 = bytes_63039;
        if (memblock_set(ctx, &res_mem_63122, &mem_63083, "mem_63083") != 0)
            return 1;
        res_58526 = 0;
        if (memblock_unref(ctx, &mem_63083, "mem_63083") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63080, "mem_63080") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63077, "mem_63077") != 0)
            return 1;
    } else {
        bool res_58538;
        int32_t res_58539;
        int32_t res_58540;
        bool loop_while_58541;
        int32_t r_58542;
        int32_t n_58543;
        
        loop_while_58541 = loop_cond_58519;
        r_58542 = 0;
        n_58543 = last_offset_58492;
        while (loop_while_58541) {
            int32_t res_58544 = sdiv32(n_58543, 2);
            int32_t res_58545 = 1 + r_58542;
            bool loop_cond_58546 = slt32(1, res_58544);
            bool loop_while_tmp_64305 = loop_cond_58546;
            int32_t r_tmp_64306 = res_58545;
            int32_t n_tmp_64307;
            
            n_tmp_64307 = res_58544;
            loop_while_58541 = loop_while_tmp_64305;
            r_58542 = r_tmp_64306;
            n_58543 = n_tmp_64307;
        }
        res_58538 = loop_while_58541;
        res_58539 = r_58542;
        res_58540 = n_58543;
        
        int32_t y_58547 = 1 << res_58539;
        bool cond_58548 = last_offset_58492 == y_58547;
        int32_t y_58549 = 1 + res_58539;
        int32_t x_58550 = 1 << y_58549;
        int32_t arg_58551 = x_58550 - last_offset_58492;
        bool bounds_invalid_upwards_58552 = slt32(arg_58551, 0);
        int32_t conc_tmp_58553 = last_offset_58492 + arg_58551;
        int32_t sizze_58554;
        
        if (cond_58548) {
            sizze_58554 = last_offset_58492;
        } else {
            sizze_58554 = conc_tmp_58553;
        }
        
        int32_t res_58555;
        
        if (cond_58548) {
            res_58555 = res_58539;
        } else {
            res_58555 = y_58549;
        }
        
        int64_t binop_x_63103 = sext_i32_i64(conc_tmp_58553);
        int64_t bytes_63102 = 4 * binop_x_63103;
        int64_t res_mem_sizze_63111;
        struct memblock res_mem_63112;
        
        res_mem_63112.references = NULL;
        
        int64_t res_mem_sizze_63113;
        struct memblock res_mem_63114;
        
        res_mem_63114.references = NULL;
        
        int64_t res_mem_sizze_63115;
        struct memblock res_mem_63116;
        
        res_mem_63116.references = NULL;
        if (cond_58548) {
            struct memblock mem_63086;
            
            mem_63086.references = NULL;
            if (memblock_alloc(ctx, &mem_63086, bytes_63039, "mem_63086"))
                return 1;
            memmove(mem_63086.mem + 0, mem_63068.mem + 0, last_offset_58492 *
                    sizeof(int32_t));
            
            struct memblock mem_63089;
            
            mem_63089.references = NULL;
            if (memblock_alloc(ctx, &mem_63089, bytes_63039, "mem_63089"))
                return 1;
            memmove(mem_63089.mem + 0, mem_63071.mem + 0, last_offset_58492 *
                    sizeof(int32_t));
            
            struct memblock mem_63092;
            
            mem_63092.references = NULL;
            if (memblock_alloc(ctx, &mem_63092, bytes_63039, "mem_63092"))
                return 1;
            memmove(mem_63092.mem + 0, mem_63074.mem + 0, last_offset_58492 *
                    sizeof(int32_t));
            res_mem_sizze_63111 = bytes_63039;
            if (memblock_set(ctx, &res_mem_63112, &mem_63086, "mem_63086") != 0)
                return 1;
            res_mem_sizze_63113 = bytes_63039;
            if (memblock_set(ctx, &res_mem_63114, &mem_63089, "mem_63089") != 0)
                return 1;
            res_mem_sizze_63115 = bytes_63039;
            if (memblock_set(ctx, &res_mem_63116, &mem_63092, "mem_63092") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63092, "mem_63092") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63089, "mem_63089") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63086, "mem_63086") != 0)
                return 1;
        } else {
            bool y_58573 = slt32(0, last_offset_58492);
            bool index_certs_58574;
            
            if (!y_58573) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                       "Index [", 0,
                                       "] out of bounds for array of shape [",
                                       last_offset_58492, "].");
                if (memblock_unref(ctx, &res_mem_63116, "res_mem_63116") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63114, "res_mem_63114") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63112, "res_mem_63112") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int32_t index_concat_58575 = *(int32_t *) &mem_63041.mem[0];
            int32_t index_concat_58576 = *(int32_t *) &mem_63044.mem[0];
            int32_t index_concat_58577 = *(int32_t *) &mem_63047.mem[0];
            int32_t res_58578;
            int32_t res_58579;
            int32_t res_58580;
            int32_t redout_61078;
            int32_t redout_61079;
            int32_t redout_61080;
            
            redout_61078 = index_concat_58575;
            redout_61079 = index_concat_58576;
            redout_61080 = index_concat_58577;
            for (int32_t i_61081 = 0; i_61081 < last_offset_58492; i_61081++) {
                bool index_concat_cmp_62107 = sle32(0, i_61081);
                int32_t index_concat_branch_62111;
                
                if (index_concat_cmp_62107) {
                    int32_t index_concat_62109 =
                            *(int32_t *) &mem_63041.mem[i_61081 * 4];
                    
                    index_concat_branch_62111 = index_concat_62109;
                } else {
                    int32_t index_concat_62110 =
                            *(int32_t *) &mem_62605.mem[i_61081 * 4];
                    
                    index_concat_branch_62111 = index_concat_62110;
                }
                
                int32_t index_concat_branch_62105;
                
                if (index_concat_cmp_62107) {
                    int32_t index_concat_62103 =
                            *(int32_t *) &mem_63044.mem[i_61081 * 4];
                    
                    index_concat_branch_62105 = index_concat_62103;
                } else {
                    int32_t index_concat_62104 =
                            *(int32_t *) &mem_62605.mem[i_61081 * 4];
                    
                    index_concat_branch_62105 = index_concat_62104;
                }
                
                int32_t index_concat_branch_62099;
                
                if (index_concat_cmp_62107) {
                    int32_t index_concat_62097 =
                            *(int32_t *) &mem_63047.mem[i_61081 * 4];
                    
                    index_concat_branch_62099 = index_concat_62097;
                } else {
                    int32_t index_concat_62098 =
                            *(int32_t *) &mem_62605.mem[i_61081 * 4];
                    
                    index_concat_branch_62099 = index_concat_62098;
                }
                
                bool cond_58587 = redout_61078 == index_concat_branch_62111;
                bool res_58588 = sle32(redout_61079, index_concat_branch_62105);
                bool res_58589 = sle32(redout_61078, index_concat_branch_62111);
                bool x_58590 = cond_58587 && res_58588;
                bool x_58591 = !cond_58587;
                bool y_58592 = res_58589 && x_58591;
                bool res_58593 = x_58590 || y_58592;
                int32_t res_58594;
                
                if (res_58593) {
                    res_58594 = index_concat_branch_62111;
                } else {
                    res_58594 = redout_61078;
                }
                
                int32_t res_58595;
                
                if (res_58593) {
                    res_58595 = index_concat_branch_62105;
                } else {
                    res_58595 = redout_61079;
                }
                
                int32_t res_58596;
                
                if (res_58593) {
                    res_58596 = index_concat_branch_62099;
                } else {
                    res_58596 = redout_61080;
                }
                
                int32_t redout_tmp_64308 = res_58594;
                int32_t redout_tmp_64309 = res_58595;
                int32_t redout_tmp_64310;
                
                redout_tmp_64310 = res_58596;
                redout_61078 = redout_tmp_64308;
                redout_61079 = redout_tmp_64309;
                redout_61080 = redout_tmp_64310;
            }
            res_58578 = redout_61078;
            res_58579 = redout_61079;
            res_58580 = redout_61080;
            
            bool eq_x_zz_58600 = 0 == arg_58551;
            bool not_p_58601 = !bounds_invalid_upwards_58552;
            bool p_and_eq_x_y_58602 = eq_x_zz_58600 && not_p_58601;
            bool dim_zzero_58603 = bounds_invalid_upwards_58552 ||
                 p_and_eq_x_y_58602;
            bool both_empty_58604 = eq_x_zz_58600 && dim_zzero_58603;
            bool eq_x_y_58605 = arg_58551 == 0;
            bool p_and_eq_x_y_58606 = bounds_invalid_upwards_58552 &&
                 eq_x_y_58605;
            bool dim_match_58607 = not_p_58601 || p_and_eq_x_y_58606;
            bool empty_or_match_58608 = both_empty_58604 || dim_match_58607;
            bool empty_or_match_cert_58609;
            
            if (!empty_or_match_58608) {
                ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                       "Function return value does not match shape of type ",
                                       "*", "[", arg_58551, "]", "t");
                if (memblock_unref(ctx, &res_mem_63116, "res_mem_63116") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63114, "res_mem_63114") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63112, "res_mem_63112") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int64_t binop_x_63094 = sext_i32_i64(arg_58551);
            int64_t bytes_63093 = 4 * binop_x_63094;
            struct memblock mem_63095;
            
            mem_63095.references = NULL;
            if (memblock_alloc(ctx, &mem_63095, bytes_63093, "mem_63095"))
                return 1;
            for (int32_t i_64311 = 0; i_64311 < arg_58551; i_64311++) {
                *(int32_t *) &mem_63095.mem[i_64311 * 4] = res_58578;
            }
            
            struct memblock mem_63098;
            
            mem_63098.references = NULL;
            if (memblock_alloc(ctx, &mem_63098, bytes_63093, "mem_63098"))
                return 1;
            for (int32_t i_64312 = 0; i_64312 < arg_58551; i_64312++) {
                *(int32_t *) &mem_63098.mem[i_64312 * 4] = res_58579;
            }
            
            struct memblock mem_63101;
            
            mem_63101.references = NULL;
            if (memblock_alloc(ctx, &mem_63101, bytes_63093, "mem_63101"))
                return 1;
            for (int32_t i_64313 = 0; i_64313 < arg_58551; i_64313++) {
                *(int32_t *) &mem_63101.mem[i_64313 * 4] = res_58580;
            }
            
            struct memblock mem_63104;
            
            mem_63104.references = NULL;
            if (memblock_alloc(ctx, &mem_63104, bytes_63102, "mem_63104"))
                return 1;
            
            int32_t tmp_offs_64314 = 0;
            
            memmove(mem_63104.mem + tmp_offs_64314 * 4, mem_62605.mem + 0, 0);
            tmp_offs_64314 = tmp_offs_64314;
            memmove(mem_63104.mem + tmp_offs_64314 * 4, mem_63041.mem + 0,
                    last_offset_58492 * sizeof(int32_t));
            tmp_offs_64314 += last_offset_58492;
            memmove(mem_63104.mem + tmp_offs_64314 * 4, mem_63095.mem + 0,
                    arg_58551 * sizeof(int32_t));
            tmp_offs_64314 += arg_58551;
            if (memblock_unref(ctx, &mem_63095, "mem_63095") != 0)
                return 1;
            
            struct memblock mem_63107;
            
            mem_63107.references = NULL;
            if (memblock_alloc(ctx, &mem_63107, bytes_63102, "mem_63107"))
                return 1;
            
            int32_t tmp_offs_64315 = 0;
            
            memmove(mem_63107.mem + tmp_offs_64315 * 4, mem_62605.mem + 0, 0);
            tmp_offs_64315 = tmp_offs_64315;
            memmove(mem_63107.mem + tmp_offs_64315 * 4, mem_63044.mem + 0,
                    last_offset_58492 * sizeof(int32_t));
            tmp_offs_64315 += last_offset_58492;
            memmove(mem_63107.mem + tmp_offs_64315 * 4, mem_63098.mem + 0,
                    arg_58551 * sizeof(int32_t));
            tmp_offs_64315 += arg_58551;
            if (memblock_unref(ctx, &mem_63098, "mem_63098") != 0)
                return 1;
            
            struct memblock mem_63110;
            
            mem_63110.references = NULL;
            if (memblock_alloc(ctx, &mem_63110, bytes_63102, "mem_63110"))
                return 1;
            
            int32_t tmp_offs_64316 = 0;
            
            memmove(mem_63110.mem + tmp_offs_64316 * 4, mem_62605.mem + 0, 0);
            tmp_offs_64316 = tmp_offs_64316;
            memmove(mem_63110.mem + tmp_offs_64316 * 4, mem_63047.mem + 0,
                    last_offset_58492 * sizeof(int32_t));
            tmp_offs_64316 += last_offset_58492;
            memmove(mem_63110.mem + tmp_offs_64316 * 4, mem_63101.mem + 0,
                    arg_58551 * sizeof(int32_t));
            tmp_offs_64316 += arg_58551;
            if (memblock_unref(ctx, &mem_63101, "mem_63101") != 0)
                return 1;
            res_mem_sizze_63111 = bytes_63102;
            if (memblock_set(ctx, &res_mem_63112, &mem_63104, "mem_63104") != 0)
                return 1;
            res_mem_sizze_63113 = bytes_63102;
            if (memblock_set(ctx, &res_mem_63114, &mem_63107, "mem_63107") != 0)
                return 1;
            res_mem_sizze_63115 = bytes_63102;
            if (memblock_set(ctx, &res_mem_63116, &mem_63110, "mem_63110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63110, "mem_63110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63107, "mem_63107") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63104, "mem_63104") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63101, "mem_63101") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63098, "mem_63098") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63095, "mem_63095") != 0)
                return 1;
        }
        sizze_58520 = sizze_58554;
        sizze_58521 = sizze_58554;
        sizze_58522 = sizze_58554;
        res_mem_sizze_63117 = res_mem_sizze_63111;
        if (memblock_set(ctx, &res_mem_63118, &res_mem_63112,
                         "res_mem_63112") != 0)
            return 1;
        res_mem_sizze_63119 = res_mem_sizze_63113;
        if (memblock_set(ctx, &res_mem_63120, &res_mem_63114,
                         "res_mem_63114") != 0)
            return 1;
        res_mem_sizze_63121 = res_mem_sizze_63115;
        if (memblock_set(ctx, &res_mem_63122, &res_mem_63116,
                         "res_mem_63116") != 0)
            return 1;
        res_58526 = res_58555;
        if (memblock_unref(ctx, &res_mem_63116, "res_mem_63116") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63114, "res_mem_63114") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63112, "res_mem_63112") != 0)
            return 1;
    }
    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
        return 1;
    
    bool dim_zzero_58619 = 0 == sizze_58521;
    bool dim_zzero_58620 = 0 == sizze_58520;
    bool both_empty_58621 = dim_zzero_58619 && dim_zzero_58620;
    bool dim_match_58622 = sizze_58520 == sizze_58521;
    bool empty_or_match_58623 = both_empty_58621 || dim_match_58622;
    bool empty_or_match_cert_58624;
    
    if (!empty_or_match_58623) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    bool dim_zzero_58626 = 0 == sizze_58522;
    bool both_empty_58627 = dim_zzero_58620 && dim_zzero_58626;
    bool dim_match_58628 = sizze_58520 == sizze_58522;
    bool empty_or_match_58629 = both_empty_58627 || dim_match_58628;
    bool empty_or_match_cert_58630;
    
    if (!empty_or_match_58629) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    int64_t binop_x_63136 = sext_i32_i64(sizze_58520);
    int64_t bytes_63135 = 4 * binop_x_63136;
    int64_t indexed_mem_sizze_63162;
    struct memblock indexed_mem_63163;
    
    indexed_mem_63163.references = NULL;
    
    int64_t indexed_mem_sizze_63164;
    struct memblock indexed_mem_63165;
    
    indexed_mem_63165.references = NULL;
    
    int64_t indexed_mem_sizze_63166;
    struct memblock indexed_mem_63167;
    
    indexed_mem_63167.references = NULL;
    
    int64_t xs_mem_sizze_63123;
    struct memblock xs_mem_63124;
    
    xs_mem_63124.references = NULL;
    
    int64_t xs_mem_sizze_63125;
    struct memblock xs_mem_63126;
    
    xs_mem_63126.references = NULL;
    
    int64_t xs_mem_sizze_63127;
    struct memblock xs_mem_63128;
    
    xs_mem_63128.references = NULL;
    xs_mem_sizze_63123 = res_mem_sizze_63117;
    if (memblock_set(ctx, &xs_mem_63124, &res_mem_63118, "res_mem_63118") != 0)
        return 1;
    xs_mem_sizze_63125 = res_mem_sizze_63119;
    if (memblock_set(ctx, &xs_mem_63126, &res_mem_63120, "res_mem_63120") != 0)
        return 1;
    xs_mem_sizze_63127 = res_mem_sizze_63121;
    if (memblock_set(ctx, &xs_mem_63128, &res_mem_63122, "res_mem_63122") != 0)
        return 1;
    for (int32_t i_58647 = 0; i_58647 < res_58526; i_58647++) {
        int32_t upper_bound_58648 = 1 + i_58647;
        int64_t res_mem_sizze_63156;
        struct memblock res_mem_63157;
        
        res_mem_63157.references = NULL;
        
        int64_t res_mem_sizze_63158;
        struct memblock res_mem_63159;
        
        res_mem_63159.references = NULL;
        
        int64_t res_mem_sizze_63160;
        struct memblock res_mem_63161;
        
        res_mem_63161.references = NULL;
        
        int64_t xs_mem_sizze_63129;
        struct memblock xs_mem_63130;
        
        xs_mem_63130.references = NULL;
        
        int64_t xs_mem_sizze_63131;
        struct memblock xs_mem_63132;
        
        xs_mem_63132.references = NULL;
        
        int64_t xs_mem_sizze_63133;
        struct memblock xs_mem_63134;
        
        xs_mem_63134.references = NULL;
        xs_mem_sizze_63129 = xs_mem_sizze_63123;
        if (memblock_set(ctx, &xs_mem_63130, &xs_mem_63124, "xs_mem_63124") !=
            0)
            return 1;
        xs_mem_sizze_63131 = xs_mem_sizze_63125;
        if (memblock_set(ctx, &xs_mem_63132, &xs_mem_63126, "xs_mem_63126") !=
            0)
            return 1;
        xs_mem_sizze_63133 = xs_mem_sizze_63127;
        if (memblock_set(ctx, &xs_mem_63134, &xs_mem_63128, "xs_mem_63128") !=
            0)
            return 1;
        for (int32_t j_58659 = 0; j_58659 < upper_bound_58648; j_58659++) {
            int32_t y_58660 = i_58647 - j_58659;
            int32_t res_58661 = 1 << y_58660;
            struct memblock mem_63137;
            
            mem_63137.references = NULL;
            if (memblock_alloc(ctx, &mem_63137, bytes_63135, "mem_63137"))
                return 1;
            
            struct memblock mem_63140;
            
            mem_63140.references = NULL;
            if (memblock_alloc(ctx, &mem_63140, bytes_63135, "mem_63140"))
                return 1;
            
            struct memblock mem_63143;
            
            mem_63143.references = NULL;
            if (memblock_alloc(ctx, &mem_63143, bytes_63135, "mem_63143"))
                return 1;
            for (int32_t i_61088 = 0; i_61088 < sizze_58520; i_61088++) {
                int32_t res_58666 = *(int32_t *) &xs_mem_63130.mem[i_61088 * 4];
                int32_t res_58667 = *(int32_t *) &xs_mem_63132.mem[i_61088 * 4];
                int32_t res_58668 = *(int32_t *) &xs_mem_63134.mem[i_61088 * 4];
                int32_t x_58669 = ashr32(i_61088, i_58647);
                int32_t x_58670 = 2 & x_58669;
                bool res_58671 = x_58670 == 0;
                int32_t x_58672 = res_58661 & i_61088;
                bool cond_58673 = x_58672 == 0;
                int32_t res_58674;
                int32_t res_58675;
                int32_t res_58676;
                
                if (cond_58673) {
                    int32_t i_58677 = res_58661 | i_61088;
                    int32_t res_58678 = *(int32_t *) &xs_mem_63130.mem[i_58677 *
                                                                       4];
                    int32_t res_58679 = *(int32_t *) &xs_mem_63132.mem[i_58677 *
                                                                       4];
                    int32_t res_58680 = *(int32_t *) &xs_mem_63134.mem[i_58677 *
                                                                       4];
                    bool cond_58681 = res_58678 == res_58666;
                    bool res_58682 = sle32(res_58679, res_58667);
                    bool res_58683 = sle32(res_58678, res_58666);
                    bool x_58684 = cond_58681 && res_58682;
                    bool x_58685 = !cond_58681;
                    bool y_58686 = res_58683 && x_58685;
                    bool res_58687 = x_58684 || y_58686;
                    bool cond_58688 = res_58687 == res_58671;
                    int32_t res_58689;
                    
                    if (cond_58688) {
                        res_58689 = res_58678;
                    } else {
                        res_58689 = res_58666;
                    }
                    
                    int32_t res_58690;
                    
                    if (cond_58688) {
                        res_58690 = res_58679;
                    } else {
                        res_58690 = res_58667;
                    }
                    
                    int32_t res_58691;
                    
                    if (cond_58688) {
                        res_58691 = res_58680;
                    } else {
                        res_58691 = res_58668;
                    }
                    res_58674 = res_58689;
                    res_58675 = res_58690;
                    res_58676 = res_58691;
                } else {
                    int32_t i_58692 = res_58661 ^ i_61088;
                    int32_t res_58693 = *(int32_t *) &xs_mem_63130.mem[i_58692 *
                                                                       4];
                    int32_t res_58694 = *(int32_t *) &xs_mem_63132.mem[i_58692 *
                                                                       4];
                    int32_t res_58695 = *(int32_t *) &xs_mem_63134.mem[i_58692 *
                                                                       4];
                    bool cond_58696 = res_58666 == res_58693;
                    bool res_58697 = sle32(res_58667, res_58694);
                    bool res_58698 = sle32(res_58666, res_58693);
                    bool x_58699 = cond_58696 && res_58697;
                    bool x_58700 = !cond_58696;
                    bool y_58701 = res_58698 && x_58700;
                    bool res_58702 = x_58699 || y_58701;
                    bool cond_58703 = res_58702 == res_58671;
                    int32_t res_58704;
                    
                    if (cond_58703) {
                        res_58704 = res_58693;
                    } else {
                        res_58704 = res_58666;
                    }
                    
                    int32_t res_58705;
                    
                    if (cond_58703) {
                        res_58705 = res_58694;
                    } else {
                        res_58705 = res_58667;
                    }
                    
                    int32_t res_58706;
                    
                    if (cond_58703) {
                        res_58706 = res_58695;
                    } else {
                        res_58706 = res_58668;
                    }
                    res_58674 = res_58704;
                    res_58675 = res_58705;
                    res_58676 = res_58706;
                }
                *(int32_t *) &mem_63137.mem[i_61088 * 4] = res_58674;
                *(int32_t *) &mem_63140.mem[i_61088 * 4] = res_58675;
                *(int32_t *) &mem_63143.mem[i_61088 * 4] = res_58676;
            }
            
            int64_t xs_mem_sizze_tmp_64326 = bytes_63135;
            struct memblock xs_mem_tmp_64327;
            
            xs_mem_tmp_64327.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64327, &mem_63137, "mem_63137") !=
                0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64328 = bytes_63135;
            struct memblock xs_mem_tmp_64329;
            
            xs_mem_tmp_64329.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64329, &mem_63140, "mem_63140") !=
                0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64330 = bytes_63135;
            struct memblock xs_mem_tmp_64331;
            
            xs_mem_tmp_64331.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64331, &mem_63143, "mem_63143") !=
                0)
                return 1;
            xs_mem_sizze_63129 = xs_mem_sizze_tmp_64326;
            if (memblock_set(ctx, &xs_mem_63130, &xs_mem_tmp_64327,
                             "xs_mem_tmp_64327") != 0)
                return 1;
            xs_mem_sizze_63131 = xs_mem_sizze_tmp_64328;
            if (memblock_set(ctx, &xs_mem_63132, &xs_mem_tmp_64329,
                             "xs_mem_tmp_64329") != 0)
                return 1;
            xs_mem_sizze_63133 = xs_mem_sizze_tmp_64330;
            if (memblock_set(ctx, &xs_mem_63134, &xs_mem_tmp_64331,
                             "xs_mem_tmp_64331") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64331, "xs_mem_tmp_64331") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64329, "xs_mem_tmp_64329") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64327, "xs_mem_tmp_64327") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63143, "mem_63143") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63140, "mem_63140") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63137, "mem_63137") != 0)
                return 1;
        }
        res_mem_sizze_63156 = xs_mem_sizze_63129;
        if (memblock_set(ctx, &res_mem_63157, &xs_mem_63130, "xs_mem_63130") !=
            0)
            return 1;
        res_mem_sizze_63158 = xs_mem_sizze_63131;
        if (memblock_set(ctx, &res_mem_63159, &xs_mem_63132, "xs_mem_63132") !=
            0)
            return 1;
        res_mem_sizze_63160 = xs_mem_sizze_63133;
        if (memblock_set(ctx, &res_mem_63161, &xs_mem_63134, "xs_mem_63134") !=
            0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_64317 = res_mem_sizze_63156;
        struct memblock xs_mem_tmp_64318;
        
        xs_mem_tmp_64318.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_64318, &res_mem_63157,
                         "res_mem_63157") != 0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_64319 = res_mem_sizze_63158;
        struct memblock xs_mem_tmp_64320;
        
        xs_mem_tmp_64320.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_64320, &res_mem_63159,
                         "res_mem_63159") != 0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_64321 = res_mem_sizze_63160;
        struct memblock xs_mem_tmp_64322;
        
        xs_mem_tmp_64322.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_64322, &res_mem_63161,
                         "res_mem_63161") != 0)
            return 1;
        xs_mem_sizze_63123 = xs_mem_sizze_tmp_64317;
        if (memblock_set(ctx, &xs_mem_63124, &xs_mem_tmp_64318,
                         "xs_mem_tmp_64318") != 0)
            return 1;
        xs_mem_sizze_63125 = xs_mem_sizze_tmp_64319;
        if (memblock_set(ctx, &xs_mem_63126, &xs_mem_tmp_64320,
                         "xs_mem_tmp_64320") != 0)
            return 1;
        xs_mem_sizze_63127 = xs_mem_sizze_tmp_64321;
        if (memblock_set(ctx, &xs_mem_63128, &xs_mem_tmp_64322,
                         "xs_mem_tmp_64322") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_64322, "xs_mem_tmp_64322") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_64320, "xs_mem_tmp_64320") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_64318, "xs_mem_tmp_64318") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63134, "xs_mem_63134") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63132, "xs_mem_63132") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63130, "xs_mem_63130") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63161, "res_mem_63161") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63159, "res_mem_63159") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63157, "res_mem_63157") != 0)
            return 1;
    }
    indexed_mem_sizze_63162 = xs_mem_sizze_63123;
    if (memblock_set(ctx, &indexed_mem_63163, &xs_mem_63124, "xs_mem_63124") !=
        0)
        return 1;
    indexed_mem_sizze_63164 = xs_mem_sizze_63125;
    if (memblock_set(ctx, &indexed_mem_63165, &xs_mem_63126, "xs_mem_63126") !=
        0)
        return 1;
    indexed_mem_sizze_63166 = xs_mem_sizze_63127;
    if (memblock_set(ctx, &indexed_mem_63167, &xs_mem_63128, "xs_mem_63128") !=
        0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
        return 1;
    
    bool i_p_m_t_s_leq_w_58707 = slt32(m_58508, sizze_58520);
    bool y_58708 = zzero_leq_i_p_m_t_s_58509 && i_p_m_t_s_leq_w_58707;
    bool ok_or_empty_58710 = empty_slice_58507 || y_58708;
    bool index_certs_58711;
    
    if (!ok_or_empty_58710) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                               "Index [", "", ":", last_offset_58492,
                               "] out of bounds for array of shape [",
                               sizze_58520, "].");
        if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    struct memblock mem_63170;
    
    mem_63170.references = NULL;
    if (memblock_alloc(ctx, &mem_63170, bytes_63039, "mem_63170"))
        return 1;
    
    struct memblock mem_63172;
    
    mem_63172.references = NULL;
    if (memblock_alloc(ctx, &mem_63172, binop_x_63040, "mem_63172"))
        return 1;
    
    int32_t discard_61115;
    int32_t scanacc_61100 = 0;
    
    for (int32_t i_61106 = 0; i_61106 < last_offset_58492; i_61106++) {
        int32_t x_58733 = *(int32_t *) &indexed_mem_63163.mem[i_61106 * 4];
        int32_t x_58734 = *(int32_t *) &indexed_mem_63165.mem[i_61106 * 4];
        int32_t i_p_o_62125 = -1 + i_61106;
        int32_t rot_i_62126 = smod32(i_p_o_62125, last_offset_58492);
        int32_t x_58735 = *(int32_t *) &indexed_mem_63163.mem[rot_i_62126 * 4];
        int32_t x_58736 = *(int32_t *) &indexed_mem_63165.mem[rot_i_62126 * 4];
        int32_t x_58737 = *(int32_t *) &indexed_mem_63167.mem[i_61106 * 4];
        bool res_58738 = x_58733 == x_58735;
        bool res_58739 = x_58734 == x_58736;
        bool eq_58740 = res_58738 && res_58739;
        bool res_58741 = !eq_58740;
        int32_t res_58731;
        
        if (res_58741) {
            res_58731 = x_58737;
        } else {
            int32_t res_58732 = x_58737 + scanacc_61100;
            
            res_58731 = res_58732;
        }
        *(int32_t *) &mem_63170.mem[i_61106 * 4] = res_58731;
        *(bool *) &mem_63172.mem[i_61106] = res_58741;
        
        int32_t scanacc_tmp_64338 = res_58731;
        
        scanacc_61100 = scanacc_tmp_64338;
    }
    discard_61115 = scanacc_61100;
    if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
        return 1;
    
    struct memblock mem_63183;
    
    mem_63183.references = NULL;
    if (memblock_alloc(ctx, &mem_63183, bytes_63039, "mem_63183"))
        return 1;
    
    int32_t discard_61121;
    int32_t scanacc_61117 = 0;
    
    for (int32_t i_61119 = 0; i_61119 < last_offset_58492; i_61119++) {
        int32_t i_p_o_62133 = 1 + i_61119;
        int32_t rot_i_62134 = smod32(i_p_o_62133, last_offset_58492);
        bool x_58747 = *(bool *) &mem_63172.mem[rot_i_62134];
        int32_t res_58748 = btoi_bool_i32(x_58747);
        int32_t res_58746 = res_58748 + scanacc_61117;
        
        *(int32_t *) &mem_63183.mem[i_61119 * 4] = res_58746;
        
        int32_t scanacc_tmp_64341 = res_58746;
        
        scanacc_61117 = scanacc_tmp_64341;
    }
    discard_61121 = scanacc_61117;
    
    int32_t res_58749;
    
    if (loop_cond_58519) {
        bool index_certs_58750;
        
        if (!zzero_leq_i_p_m_t_s_58509) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                   "Index [", m_58508,
                                   "] out of bounds for array of shape [",
                                   last_offset_58492, "].");
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t res_58751 = *(int32_t *) &mem_63183.mem[m_58508 * 4];
        
        res_58749 = res_58751;
    } else {
        res_58749 = 0;
    }
    
    bool bounds_invalid_upwards_58752 = slt32(res_58749, 0);
    bool eq_x_zz_58753 = 0 == res_58749;
    bool not_p_58754 = !bounds_invalid_upwards_58752;
    bool p_and_eq_x_y_58755 = eq_x_zz_58753 && not_p_58754;
    bool dim_zzero_58756 = bounds_invalid_upwards_58752 || p_and_eq_x_y_58755;
    bool both_empty_58757 = eq_x_zz_58753 && dim_zzero_58756;
    bool eq_x_y_58758 = res_58749 == 0;
    bool empty_or_match_58761 = not_p_58754 || both_empty_58757;
    bool empty_or_match_cert_58762;
    
    if (!empty_or_match_58761) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:14-19 -> tupleTest.fut:134:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                               "Function return value does not match shape of type ",
                               "*", "[", res_58749, "]", "t");
        if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    int64_t binop_x_63189 = sext_i32_i64(res_58749);
    int64_t bytes_63188 = 4 * binop_x_63189;
    struct memblock mem_63190;
    
    mem_63190.references = NULL;
    if (memblock_alloc(ctx, &mem_63190, bytes_63188, "mem_63190"))
        return 1;
    for (int32_t i_64343 = 0; i_64343 < res_58749; i_64343++) {
        *(int32_t *) &mem_63190.mem[i_64343 * 4] = 0;
    }
    for (int32_t write_iter_61122 = 0; write_iter_61122 < last_offset_58492;
         write_iter_61122++) {
        int32_t write_iv_61126 = *(int32_t *) &mem_63183.mem[write_iter_61122 *
                                                             4];
        int32_t i_p_o_62138 = 1 + write_iter_61122;
        int32_t rot_i_62139 = smod32(i_p_o_62138, last_offset_58492);
        bool write_iv_61127 = *(bool *) &mem_63172.mem[rot_i_62139];
        int32_t res_58774;
        
        if (write_iv_61127) {
            int32_t res_58775 = write_iv_61126 - 1;
            
            res_58774 = res_58775;
        } else {
            res_58774 = -1;
        }
        
        bool less_than_zzero_61143 = slt32(res_58774, 0);
        bool greater_than_sizze_61144 = sle32(res_58749, res_58774);
        bool outside_bounds_dim_61145 = less_than_zzero_61143 ||
             greater_than_sizze_61144;
        
        if (!outside_bounds_dim_61145) {
            memmove(mem_63190.mem + res_58774 * 4, mem_63170.mem +
                    write_iter_61122 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
        return 1;
    
    struct memblock mem_63197;
    
    mem_63197.references = NULL;
    if (memblock_alloc(ctx, &mem_63197, bytes_63188, "mem_63197"))
        return 1;
    
    struct memblock mem_63200;
    
    mem_63200.references = NULL;
    if (memblock_alloc(ctx, &mem_63200, bytes_63188, "mem_63200"))
        return 1;
    
    int32_t discard_61157;
    int32_t scanacc_61151 = 0;
    
    for (int32_t i_61154 = 0; i_61154 < res_58749; i_61154++) {
        int32_t x_58781 = *(int32_t *) &mem_63190.mem[i_61154 * 4];
        bool not_arg_58782 = x_58781 == 0;
        bool res_58783 = !not_arg_58782;
        int32_t part_res_58784;
        
        if (res_58783) {
            part_res_58784 = 0;
        } else {
            part_res_58784 = 1;
        }
        
        int32_t part_res_58785;
        
        if (res_58783) {
            part_res_58785 = 1;
        } else {
            part_res_58785 = 0;
        }
        
        int32_t zz_58780 = part_res_58785 + scanacc_61151;
        
        *(int32_t *) &mem_63197.mem[i_61154 * 4] = zz_58780;
        *(int32_t *) &mem_63200.mem[i_61154 * 4] = part_res_58784;
        
        int32_t scanacc_tmp_64345 = zz_58780;
        
        scanacc_61151 = scanacc_tmp_64345;
    }
    discard_61157 = scanacc_61151;
    
    int32_t last_index_58786 = res_58749 - 1;
    int32_t partition_sizze_58787;
    
    if (eq_x_y_58758) {
        partition_sizze_58787 = 0;
    } else {
        int32_t last_offset_58788 =
                *(int32_t *) &mem_63197.mem[last_index_58786 * 4];
        
        partition_sizze_58787 = last_offset_58788;
    }
    
    int64_t binop_x_63210 = sext_i32_i64(partition_sizze_58787);
    int64_t bytes_63209 = 4 * binop_x_63210;
    struct memblock mem_63211;
    
    mem_63211.references = NULL;
    if (memblock_alloc(ctx, &mem_63211, bytes_63209, "mem_63211"))
        return 1;
    for (int32_t write_iter_61158 = 0; write_iter_61158 < res_58749;
         write_iter_61158++) {
        int32_t write_iv_61160 = *(int32_t *) &mem_63200.mem[write_iter_61158 *
                                                             4];
        int32_t write_iv_61161 = *(int32_t *) &mem_63197.mem[write_iter_61158 *
                                                             4];
        bool is_this_one_58796 = write_iv_61160 == 0;
        int32_t this_offset_58797 = -1 + write_iv_61161;
        int32_t total_res_58798;
        
        if (is_this_one_58796) {
            total_res_58798 = this_offset_58797;
        } else {
            total_res_58798 = -1;
        }
        
        bool less_than_zzero_61165 = slt32(total_res_58798, 0);
        bool greater_than_sizze_61166 = sle32(partition_sizze_58787,
                                              total_res_58798);
        bool outside_bounds_dim_61167 = less_than_zzero_61165 ||
             greater_than_sizze_61166;
        
        if (!outside_bounds_dim_61167) {
            memmove(mem_63211.mem + total_res_58798 * 4, mem_63190.mem +
                    write_iter_61158 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
        return 1;
    
    bool cond_58799 = partition_sizze_58787 == 5;
    bool res_58800;
    
    if (cond_58799) {
        bool arrays_equal_58801;
        
        if (cond_58799) {
            bool all_equal_58803;
            bool redout_61171 = 1;
            
            for (int32_t i_61172 = 0; i_61172 < partition_sizze_58787;
                 i_61172++) {
                int32_t x_58808 = *(int32_t *) &mem_63211.mem[i_61172 * 4];
                int32_t res_58809 = 1 + i_61172;
                bool res_58810 = x_58808 == res_58809;
                bool res_58806 = res_58810 && redout_61171;
                bool redout_tmp_64349 = res_58806;
                
                redout_61171 = redout_tmp_64349;
            }
            all_equal_58803 = redout_61171;
            arrays_equal_58801 = all_equal_58803;
        } else {
            arrays_equal_58801 = 0;
        }
        res_58800 = arrays_equal_58801;
    } else {
        res_58800 = 0;
    }
    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
        return 1;
    
    bool cond_58811;
    
    if (res_58800) {
        struct memblock mem_63218;
        
        mem_63218.references = NULL;
        if (memblock_alloc(ctx, &mem_63218, 24, "mem_63218"))
            return 1;
        
        struct memblock mem_63223;
        
        mem_63223.references = NULL;
        if (memblock_alloc(ctx, &mem_63223, 8, "mem_63223"))
            return 1;
        for (int32_t i_61175 = 0; i_61175 < 3; i_61175++) {
            for (int32_t i_64351 = 0; i_64351 < 2; i_64351++) {
                *(int32_t *) &mem_63223.mem[i_64351 * 4] = i_61175;
            }
            memmove(mem_63218.mem + 2 * i_61175 * 4, mem_63223.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
            return 1;
        
        struct memblock mem_63228;
        
        mem_63228.references = NULL;
        if (memblock_alloc(ctx, &mem_63228, 24, "mem_63228"))
            return 1;
        
        struct memblock mem_63231;
        
        mem_63231.references = NULL;
        if (memblock_alloc(ctx, &mem_63231, 24, "mem_63231"))
            return 1;
        
        int32_t discard_61185;
        int32_t scanacc_61179 = 0;
        
        for (int32_t i_61182 = 0; i_61182 < 6; i_61182++) {
            bool not_arg_58822 = i_61182 == 0;
            bool res_58823 = !not_arg_58822;
            int32_t part_res_58824;
            
            if (res_58823) {
                part_res_58824 = 0;
            } else {
                part_res_58824 = 1;
            }
            
            int32_t part_res_58825;
            
            if (res_58823) {
                part_res_58825 = 1;
            } else {
                part_res_58825 = 0;
            }
            
            int32_t zz_58820 = part_res_58825 + scanacc_61179;
            
            *(int32_t *) &mem_63228.mem[i_61182 * 4] = zz_58820;
            *(int32_t *) &mem_63231.mem[i_61182 * 4] = part_res_58824;
            
            int32_t scanacc_tmp_64352 = zz_58820;
            
            scanacc_61179 = scanacc_tmp_64352;
        }
        discard_61185 = scanacc_61179;
        
        int32_t last_offset_58826 = *(int32_t *) &mem_63228.mem[20];
        int64_t binop_x_63241 = sext_i32_i64(last_offset_58826);
        int64_t bytes_63240 = 4 * binop_x_63241;
        struct memblock mem_63242;
        
        mem_63242.references = NULL;
        if (memblock_alloc(ctx, &mem_63242, bytes_63240, "mem_63242"))
            return 1;
        
        struct memblock mem_63245;
        
        mem_63245.references = NULL;
        if (memblock_alloc(ctx, &mem_63245, bytes_63240, "mem_63245"))
            return 1;
        
        struct memblock mem_63248;
        
        mem_63248.references = NULL;
        if (memblock_alloc(ctx, &mem_63248, bytes_63240, "mem_63248"))
            return 1;
        for (int32_t write_iter_61186 = 0; write_iter_61186 < 6;
             write_iter_61186++) {
            int32_t write_iv_61190 =
                    *(int32_t *) &mem_63231.mem[write_iter_61186 * 4];
            int32_t write_iv_61191 =
                    *(int32_t *) &mem_63228.mem[write_iter_61186 * 4];
            int32_t new_index_62144 = squot32(write_iter_61186, 2);
            int32_t binop_y_62146 = 2 * new_index_62144;
            int32_t new_index_62147 = write_iter_61186 - binop_y_62146;
            bool is_this_one_58838 = write_iv_61190 == 0;
            int32_t this_offset_58839 = -1 + write_iv_61191;
            int32_t total_res_58840;
            
            if (is_this_one_58838) {
                total_res_58840 = this_offset_58839;
            } else {
                total_res_58840 = -1;
            }
            
            bool less_than_zzero_61195 = slt32(total_res_58840, 0);
            bool greater_than_sizze_61196 = sle32(last_offset_58826,
                                                  total_res_58840);
            bool outside_bounds_dim_61197 = less_than_zzero_61195 ||
                 greater_than_sizze_61196;
            
            if (!outside_bounds_dim_61197) {
                memmove(mem_63242.mem + total_res_58840 * 4, mem_63218.mem +
                        (2 * new_index_62144 + new_index_62147) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_61197) {
                struct memblock mem_63257;
                
                mem_63257.references = NULL;
                if (memblock_alloc(ctx, &mem_63257, 4, "mem_63257"))
                    return 1;
                
                int32_t x_64359;
                
                for (int32_t i_64358 = 0; i_64358 < 1; i_64358++) {
                    x_64359 = new_index_62147 + sext_i32_i32(i_64358);
                    *(int32_t *) &mem_63257.mem[i_64358 * 4] = x_64359;
                }
                memmove(mem_63245.mem + total_res_58840 * 4, mem_63257.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63257, "mem_63257") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63257, "mem_63257") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_61197) {
                struct memblock mem_63260;
                
                mem_63260.references = NULL;
                if (memblock_alloc(ctx, &mem_63260, 4, "mem_63260"))
                    return 1;
                
                int32_t x_64361;
                
                for (int32_t i_64360 = 0; i_64360 < 1; i_64360++) {
                    x_64361 = write_iter_61186 + sext_i32_i32(i_64360);
                    *(int32_t *) &mem_63260.mem[i_64360 * 4] = x_64361;
                }
                memmove(mem_63248.mem + total_res_58840 * 4, mem_63260.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63260, "mem_63260") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63260, "mem_63260") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
            return 1;
        
        bool empty_slice_58841 = last_offset_58826 == 0;
        int32_t m_58842 = last_offset_58826 - 1;
        bool zzero_leq_i_p_m_t_s_58843 = sle32(0, m_58842);
        struct memblock mem_63269;
        
        mem_63269.references = NULL;
        if (memblock_alloc(ctx, &mem_63269, bytes_63240, "mem_63269"))
            return 1;
        
        int32_t tmp_offs_64362 = 0;
        
        memmove(mem_63269.mem + tmp_offs_64362 * 4, mem_62605.mem + 0, 0);
        tmp_offs_64362 = tmp_offs_64362;
        memmove(mem_63269.mem + tmp_offs_64362 * 4, mem_63242.mem + 0,
                last_offset_58826 * sizeof(int32_t));
        tmp_offs_64362 += last_offset_58826;
        
        struct memblock mem_63272;
        
        mem_63272.references = NULL;
        if (memblock_alloc(ctx, &mem_63272, bytes_63240, "mem_63272"))
            return 1;
        
        int32_t tmp_offs_64363 = 0;
        
        memmove(mem_63272.mem + tmp_offs_64363 * 4, mem_62605.mem + 0, 0);
        tmp_offs_64363 = tmp_offs_64363;
        memmove(mem_63272.mem + tmp_offs_64363 * 4, mem_63245.mem + 0,
                last_offset_58826 * sizeof(int32_t));
        tmp_offs_64363 += last_offset_58826;
        
        struct memblock mem_63275;
        
        mem_63275.references = NULL;
        if (memblock_alloc(ctx, &mem_63275, bytes_63240, "mem_63275"))
            return 1;
        
        int32_t tmp_offs_64364 = 0;
        
        memmove(mem_63275.mem + tmp_offs_64364 * 4, mem_62605.mem + 0, 0);
        tmp_offs_64364 = tmp_offs_64364;
        memmove(mem_63275.mem + tmp_offs_64364 * 4, mem_63248.mem + 0,
                last_offset_58826 * sizeof(int32_t));
        tmp_offs_64364 += last_offset_58826;
        
        bool loop_cond_58853 = slt32(1, last_offset_58826);
        int32_t sizze_58854;
        int32_t sizze_58855;
        int32_t sizze_58856;
        int64_t res_mem_sizze_63318;
        struct memblock res_mem_63319;
        
        res_mem_63319.references = NULL;
        
        int64_t res_mem_sizze_63320;
        struct memblock res_mem_63321;
        
        res_mem_63321.references = NULL;
        
        int64_t res_mem_sizze_63322;
        struct memblock res_mem_63323;
        
        res_mem_63323.references = NULL;
        
        int32_t res_58860;
        
        if (empty_slice_58841) {
            struct memblock mem_63278;
            
            mem_63278.references = NULL;
            if (memblock_alloc(ctx, &mem_63278, bytes_63240, "mem_63278"))
                return 1;
            memmove(mem_63278.mem + 0, mem_63269.mem + 0, last_offset_58826 *
                    sizeof(int32_t));
            
            struct memblock mem_63281;
            
            mem_63281.references = NULL;
            if (memblock_alloc(ctx, &mem_63281, bytes_63240, "mem_63281"))
                return 1;
            memmove(mem_63281.mem + 0, mem_63272.mem + 0, last_offset_58826 *
                    sizeof(int32_t));
            
            struct memblock mem_63284;
            
            mem_63284.references = NULL;
            if (memblock_alloc(ctx, &mem_63284, bytes_63240, "mem_63284"))
                return 1;
            memmove(mem_63284.mem + 0, mem_63275.mem + 0, last_offset_58826 *
                    sizeof(int32_t));
            sizze_58854 = last_offset_58826;
            sizze_58855 = last_offset_58826;
            sizze_58856 = last_offset_58826;
            res_mem_sizze_63318 = bytes_63240;
            if (memblock_set(ctx, &res_mem_63319, &mem_63278, "mem_63278") != 0)
                return 1;
            res_mem_sizze_63320 = bytes_63240;
            if (memblock_set(ctx, &res_mem_63321, &mem_63281, "mem_63281") != 0)
                return 1;
            res_mem_sizze_63322 = bytes_63240;
            if (memblock_set(ctx, &res_mem_63323, &mem_63284, "mem_63284") != 0)
                return 1;
            res_58860 = 0;
            if (memblock_unref(ctx, &mem_63284, "mem_63284") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63281, "mem_63281") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63278, "mem_63278") != 0)
                return 1;
        } else {
            bool res_58872;
            int32_t res_58873;
            int32_t res_58874;
            bool loop_while_58875;
            int32_t r_58876;
            int32_t n_58877;
            
            loop_while_58875 = loop_cond_58853;
            r_58876 = 0;
            n_58877 = last_offset_58826;
            while (loop_while_58875) {
                int32_t res_58878 = sdiv32(n_58877, 2);
                int32_t res_58879 = 1 + r_58876;
                bool loop_cond_58880 = slt32(1, res_58878);
                bool loop_while_tmp_64365 = loop_cond_58880;
                int32_t r_tmp_64366 = res_58879;
                int32_t n_tmp_64367;
                
                n_tmp_64367 = res_58878;
                loop_while_58875 = loop_while_tmp_64365;
                r_58876 = r_tmp_64366;
                n_58877 = n_tmp_64367;
            }
            res_58872 = loop_while_58875;
            res_58873 = r_58876;
            res_58874 = n_58877;
            
            int32_t y_58881 = 1 << res_58873;
            bool cond_58882 = last_offset_58826 == y_58881;
            int32_t y_58883 = 1 + res_58873;
            int32_t x_58884 = 1 << y_58883;
            int32_t arg_58885 = x_58884 - last_offset_58826;
            bool bounds_invalid_upwards_58886 = slt32(arg_58885, 0);
            int32_t conc_tmp_58887 = last_offset_58826 + arg_58885;
            int32_t sizze_58888;
            
            if (cond_58882) {
                sizze_58888 = last_offset_58826;
            } else {
                sizze_58888 = conc_tmp_58887;
            }
            
            int32_t res_58889;
            
            if (cond_58882) {
                res_58889 = res_58873;
            } else {
                res_58889 = y_58883;
            }
            
            int64_t binop_x_63304 = sext_i32_i64(conc_tmp_58887);
            int64_t bytes_63303 = 4 * binop_x_63304;
            int64_t res_mem_sizze_63312;
            struct memblock res_mem_63313;
            
            res_mem_63313.references = NULL;
            
            int64_t res_mem_sizze_63314;
            struct memblock res_mem_63315;
            
            res_mem_63315.references = NULL;
            
            int64_t res_mem_sizze_63316;
            struct memblock res_mem_63317;
            
            res_mem_63317.references = NULL;
            if (cond_58882) {
                struct memblock mem_63287;
                
                mem_63287.references = NULL;
                if (memblock_alloc(ctx, &mem_63287, bytes_63240, "mem_63287"))
                    return 1;
                memmove(mem_63287.mem + 0, mem_63269.mem + 0,
                        last_offset_58826 * sizeof(int32_t));
                
                struct memblock mem_63290;
                
                mem_63290.references = NULL;
                if (memblock_alloc(ctx, &mem_63290, bytes_63240, "mem_63290"))
                    return 1;
                memmove(mem_63290.mem + 0, mem_63272.mem + 0,
                        last_offset_58826 * sizeof(int32_t));
                
                struct memblock mem_63293;
                
                mem_63293.references = NULL;
                if (memblock_alloc(ctx, &mem_63293, bytes_63240, "mem_63293"))
                    return 1;
                memmove(mem_63293.mem + 0, mem_63275.mem + 0,
                        last_offset_58826 * sizeof(int32_t));
                res_mem_sizze_63312 = bytes_63240;
                if (memblock_set(ctx, &res_mem_63313, &mem_63287,
                                 "mem_63287") != 0)
                    return 1;
                res_mem_sizze_63314 = bytes_63240;
                if (memblock_set(ctx, &res_mem_63315, &mem_63290,
                                 "mem_63290") != 0)
                    return 1;
                res_mem_sizze_63316 = bytes_63240;
                if (memblock_set(ctx, &res_mem_63317, &mem_63293,
                                 "mem_63293") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63293, "mem_63293") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63290, "mem_63290") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63287, "mem_63287") != 0)
                    return 1;
            } else {
                bool y_58907 = slt32(0, last_offset_58826);
                bool index_certs_58908;
                
                if (!y_58907) {
                    ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                           "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                           "Index [", 0,
                                           "] out of bounds for array of shape [",
                                           last_offset_58826, "].");
                    if (memblock_unref(ctx, &res_mem_63317, "res_mem_63317") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63315, "res_mem_63315") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63313, "res_mem_63313") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63167,
                                       "indexed_mem_63167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63165,
                                       "indexed_mem_63165") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63163,
                                       "indexed_mem_63163") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                        return 1;
                    return 1;
                }
                
                int32_t index_concat_58909 = *(int32_t *) &mem_63242.mem[0];
                int32_t index_concat_58910 = *(int32_t *) &mem_63245.mem[0];
                int32_t index_concat_58911 = *(int32_t *) &mem_63248.mem[0];
                int32_t res_58912;
                int32_t res_58913;
                int32_t res_58914;
                int32_t redout_61213;
                int32_t redout_61214;
                int32_t redout_61215;
                
                redout_61213 = index_concat_58909;
                redout_61214 = index_concat_58910;
                redout_61215 = index_concat_58911;
                for (int32_t i_61216 = 0; i_61216 < last_offset_58826;
                     i_61216++) {
                    bool index_concat_cmp_62174 = sle32(0, i_61216);
                    int32_t index_concat_branch_62178;
                    
                    if (index_concat_cmp_62174) {
                        int32_t index_concat_62176 =
                                *(int32_t *) &mem_63242.mem[i_61216 * 4];
                        
                        index_concat_branch_62178 = index_concat_62176;
                    } else {
                        int32_t index_concat_62177 =
                                *(int32_t *) &mem_62605.mem[i_61216 * 4];
                        
                        index_concat_branch_62178 = index_concat_62177;
                    }
                    
                    int32_t index_concat_branch_62172;
                    
                    if (index_concat_cmp_62174) {
                        int32_t index_concat_62170 =
                                *(int32_t *) &mem_63245.mem[i_61216 * 4];
                        
                        index_concat_branch_62172 = index_concat_62170;
                    } else {
                        int32_t index_concat_62171 =
                                *(int32_t *) &mem_62605.mem[i_61216 * 4];
                        
                        index_concat_branch_62172 = index_concat_62171;
                    }
                    
                    int32_t index_concat_branch_62166;
                    
                    if (index_concat_cmp_62174) {
                        int32_t index_concat_62164 =
                                *(int32_t *) &mem_63248.mem[i_61216 * 4];
                        
                        index_concat_branch_62166 = index_concat_62164;
                    } else {
                        int32_t index_concat_62165 =
                                *(int32_t *) &mem_62605.mem[i_61216 * 4];
                        
                        index_concat_branch_62166 = index_concat_62165;
                    }
                    
                    bool cond_58921 = redout_61213 == index_concat_branch_62178;
                    bool res_58922 = sle32(redout_61214,
                                           index_concat_branch_62172);
                    bool res_58923 = sle32(redout_61213,
                                           index_concat_branch_62178);
                    bool x_58924 = cond_58921 && res_58922;
                    bool x_58925 = !cond_58921;
                    bool y_58926 = res_58923 && x_58925;
                    bool res_58927 = x_58924 || y_58926;
                    int32_t res_58928;
                    
                    if (res_58927) {
                        res_58928 = index_concat_branch_62178;
                    } else {
                        res_58928 = redout_61213;
                    }
                    
                    int32_t res_58929;
                    
                    if (res_58927) {
                        res_58929 = index_concat_branch_62172;
                    } else {
                        res_58929 = redout_61214;
                    }
                    
                    int32_t res_58930;
                    
                    if (res_58927) {
                        res_58930 = index_concat_branch_62166;
                    } else {
                        res_58930 = redout_61215;
                    }
                    
                    int32_t redout_tmp_64368 = res_58928;
                    int32_t redout_tmp_64369 = res_58929;
                    int32_t redout_tmp_64370;
                    
                    redout_tmp_64370 = res_58930;
                    redout_61213 = redout_tmp_64368;
                    redout_61214 = redout_tmp_64369;
                    redout_61215 = redout_tmp_64370;
                }
                res_58912 = redout_61213;
                res_58913 = redout_61214;
                res_58914 = redout_61215;
                
                bool eq_x_zz_58934 = 0 == arg_58885;
                bool not_p_58935 = !bounds_invalid_upwards_58886;
                bool p_and_eq_x_y_58936 = eq_x_zz_58934 && not_p_58935;
                bool dim_zzero_58937 = bounds_invalid_upwards_58886 ||
                     p_and_eq_x_y_58936;
                bool both_empty_58938 = eq_x_zz_58934 && dim_zzero_58937;
                bool eq_x_y_58939 = arg_58885 == 0;
                bool p_and_eq_x_y_58940 = bounds_invalid_upwards_58886 &&
                     eq_x_y_58939;
                bool dim_match_58941 = not_p_58935 || p_and_eq_x_y_58940;
                bool empty_or_match_58942 = both_empty_58938 || dim_match_58941;
                bool empty_or_match_cert_58943;
                
                if (!empty_or_match_58942) {
                    ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                           "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                           "Function return value does not match shape of type ",
                                           "*", "[", arg_58885, "]", "t");
                    if (memblock_unref(ctx, &res_mem_63317, "res_mem_63317") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63315, "res_mem_63315") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63313, "res_mem_63313") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63167,
                                       "indexed_mem_63167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63165,
                                       "indexed_mem_63165") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63163,
                                       "indexed_mem_63163") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                        return 1;
                    return 1;
                }
                
                int64_t binop_x_63295 = sext_i32_i64(arg_58885);
                int64_t bytes_63294 = 4 * binop_x_63295;
                struct memblock mem_63296;
                
                mem_63296.references = NULL;
                if (memblock_alloc(ctx, &mem_63296, bytes_63294, "mem_63296"))
                    return 1;
                for (int32_t i_64371 = 0; i_64371 < arg_58885; i_64371++) {
                    *(int32_t *) &mem_63296.mem[i_64371 * 4] = res_58912;
                }
                
                struct memblock mem_63299;
                
                mem_63299.references = NULL;
                if (memblock_alloc(ctx, &mem_63299, bytes_63294, "mem_63299"))
                    return 1;
                for (int32_t i_64372 = 0; i_64372 < arg_58885; i_64372++) {
                    *(int32_t *) &mem_63299.mem[i_64372 * 4] = res_58913;
                }
                
                struct memblock mem_63302;
                
                mem_63302.references = NULL;
                if (memblock_alloc(ctx, &mem_63302, bytes_63294, "mem_63302"))
                    return 1;
                for (int32_t i_64373 = 0; i_64373 < arg_58885; i_64373++) {
                    *(int32_t *) &mem_63302.mem[i_64373 * 4] = res_58914;
                }
                
                struct memblock mem_63305;
                
                mem_63305.references = NULL;
                if (memblock_alloc(ctx, &mem_63305, bytes_63303, "mem_63305"))
                    return 1;
                
                int32_t tmp_offs_64374 = 0;
                
                memmove(mem_63305.mem + tmp_offs_64374 * 4, mem_62605.mem + 0,
                        0);
                tmp_offs_64374 = tmp_offs_64374;
                memmove(mem_63305.mem + tmp_offs_64374 * 4, mem_63242.mem + 0,
                        last_offset_58826 * sizeof(int32_t));
                tmp_offs_64374 += last_offset_58826;
                memmove(mem_63305.mem + tmp_offs_64374 * 4, mem_63296.mem + 0,
                        arg_58885 * sizeof(int32_t));
                tmp_offs_64374 += arg_58885;
                if (memblock_unref(ctx, &mem_63296, "mem_63296") != 0)
                    return 1;
                
                struct memblock mem_63308;
                
                mem_63308.references = NULL;
                if (memblock_alloc(ctx, &mem_63308, bytes_63303, "mem_63308"))
                    return 1;
                
                int32_t tmp_offs_64375 = 0;
                
                memmove(mem_63308.mem + tmp_offs_64375 * 4, mem_62605.mem + 0,
                        0);
                tmp_offs_64375 = tmp_offs_64375;
                memmove(mem_63308.mem + tmp_offs_64375 * 4, mem_63245.mem + 0,
                        last_offset_58826 * sizeof(int32_t));
                tmp_offs_64375 += last_offset_58826;
                memmove(mem_63308.mem + tmp_offs_64375 * 4, mem_63299.mem + 0,
                        arg_58885 * sizeof(int32_t));
                tmp_offs_64375 += arg_58885;
                if (memblock_unref(ctx, &mem_63299, "mem_63299") != 0)
                    return 1;
                
                struct memblock mem_63311;
                
                mem_63311.references = NULL;
                if (memblock_alloc(ctx, &mem_63311, bytes_63303, "mem_63311"))
                    return 1;
                
                int32_t tmp_offs_64376 = 0;
                
                memmove(mem_63311.mem + tmp_offs_64376 * 4, mem_62605.mem + 0,
                        0);
                tmp_offs_64376 = tmp_offs_64376;
                memmove(mem_63311.mem + tmp_offs_64376 * 4, mem_63248.mem + 0,
                        last_offset_58826 * sizeof(int32_t));
                tmp_offs_64376 += last_offset_58826;
                memmove(mem_63311.mem + tmp_offs_64376 * 4, mem_63302.mem + 0,
                        arg_58885 * sizeof(int32_t));
                tmp_offs_64376 += arg_58885;
                if (memblock_unref(ctx, &mem_63302, "mem_63302") != 0)
                    return 1;
                res_mem_sizze_63312 = bytes_63303;
                if (memblock_set(ctx, &res_mem_63313, &mem_63305,
                                 "mem_63305") != 0)
                    return 1;
                res_mem_sizze_63314 = bytes_63303;
                if (memblock_set(ctx, &res_mem_63315, &mem_63308,
                                 "mem_63308") != 0)
                    return 1;
                res_mem_sizze_63316 = bytes_63303;
                if (memblock_set(ctx, &res_mem_63317, &mem_63311,
                                 "mem_63311") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63311, "mem_63311") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63308, "mem_63308") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63305, "mem_63305") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63302, "mem_63302") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63299, "mem_63299") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63296, "mem_63296") != 0)
                    return 1;
            }
            sizze_58854 = sizze_58888;
            sizze_58855 = sizze_58888;
            sizze_58856 = sizze_58888;
            res_mem_sizze_63318 = res_mem_sizze_63312;
            if (memblock_set(ctx, &res_mem_63319, &res_mem_63313,
                             "res_mem_63313") != 0)
                return 1;
            res_mem_sizze_63320 = res_mem_sizze_63314;
            if (memblock_set(ctx, &res_mem_63321, &res_mem_63315,
                             "res_mem_63315") != 0)
                return 1;
            res_mem_sizze_63322 = res_mem_sizze_63316;
            if (memblock_set(ctx, &res_mem_63323, &res_mem_63317,
                             "res_mem_63317") != 0)
                return 1;
            res_58860 = res_58889;
            if (memblock_unref(ctx, &res_mem_63317, "res_mem_63317") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63315, "res_mem_63315") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63313, "res_mem_63313") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
            return 1;
        
        bool dim_zzero_58953 = 0 == sizze_58855;
        bool dim_zzero_58954 = 0 == sizze_58854;
        bool both_empty_58955 = dim_zzero_58953 && dim_zzero_58954;
        bool dim_match_58956 = sizze_58854 == sizze_58855;
        bool empty_or_match_58957 = both_empty_58955 || dim_match_58956;
        bool empty_or_match_cert_58958;
        
        if (!empty_or_match_58957) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        bool dim_zzero_58960 = 0 == sizze_58856;
        bool both_empty_58961 = dim_zzero_58954 && dim_zzero_58960;
        bool dim_match_58962 = sizze_58854 == sizze_58856;
        bool empty_or_match_58963 = both_empty_58961 || dim_match_58962;
        bool empty_or_match_cert_58964;
        
        if (!empty_or_match_58963) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_63337 = sext_i32_i64(sizze_58854);
        int64_t bytes_63336 = 4 * binop_x_63337;
        int64_t indexed_mem_sizze_63363;
        struct memblock indexed_mem_63364;
        
        indexed_mem_63364.references = NULL;
        
        int64_t indexed_mem_sizze_63365;
        struct memblock indexed_mem_63366;
        
        indexed_mem_63366.references = NULL;
        
        int64_t indexed_mem_sizze_63367;
        struct memblock indexed_mem_63368;
        
        indexed_mem_63368.references = NULL;
        
        int64_t xs_mem_sizze_63324;
        struct memblock xs_mem_63325;
        
        xs_mem_63325.references = NULL;
        
        int64_t xs_mem_sizze_63326;
        struct memblock xs_mem_63327;
        
        xs_mem_63327.references = NULL;
        
        int64_t xs_mem_sizze_63328;
        struct memblock xs_mem_63329;
        
        xs_mem_63329.references = NULL;
        xs_mem_sizze_63324 = res_mem_sizze_63318;
        if (memblock_set(ctx, &xs_mem_63325, &res_mem_63319, "res_mem_63319") !=
            0)
            return 1;
        xs_mem_sizze_63326 = res_mem_sizze_63320;
        if (memblock_set(ctx, &xs_mem_63327, &res_mem_63321, "res_mem_63321") !=
            0)
            return 1;
        xs_mem_sizze_63328 = res_mem_sizze_63322;
        if (memblock_set(ctx, &xs_mem_63329, &res_mem_63323, "res_mem_63323") !=
            0)
            return 1;
        for (int32_t i_58981 = 0; i_58981 < res_58860; i_58981++) {
            int32_t upper_bound_58982 = 1 + i_58981;
            int64_t res_mem_sizze_63357;
            struct memblock res_mem_63358;
            
            res_mem_63358.references = NULL;
            
            int64_t res_mem_sizze_63359;
            struct memblock res_mem_63360;
            
            res_mem_63360.references = NULL;
            
            int64_t res_mem_sizze_63361;
            struct memblock res_mem_63362;
            
            res_mem_63362.references = NULL;
            
            int64_t xs_mem_sizze_63330;
            struct memblock xs_mem_63331;
            
            xs_mem_63331.references = NULL;
            
            int64_t xs_mem_sizze_63332;
            struct memblock xs_mem_63333;
            
            xs_mem_63333.references = NULL;
            
            int64_t xs_mem_sizze_63334;
            struct memblock xs_mem_63335;
            
            xs_mem_63335.references = NULL;
            xs_mem_sizze_63330 = xs_mem_sizze_63324;
            if (memblock_set(ctx, &xs_mem_63331, &xs_mem_63325,
                             "xs_mem_63325") != 0)
                return 1;
            xs_mem_sizze_63332 = xs_mem_sizze_63326;
            if (memblock_set(ctx, &xs_mem_63333, &xs_mem_63327,
                             "xs_mem_63327") != 0)
                return 1;
            xs_mem_sizze_63334 = xs_mem_sizze_63328;
            if (memblock_set(ctx, &xs_mem_63335, &xs_mem_63329,
                             "xs_mem_63329") != 0)
                return 1;
            for (int32_t j_58993 = 0; j_58993 < upper_bound_58982; j_58993++) {
                int32_t y_58994 = i_58981 - j_58993;
                int32_t res_58995 = 1 << y_58994;
                struct memblock mem_63338;
                
                mem_63338.references = NULL;
                if (memblock_alloc(ctx, &mem_63338, bytes_63336, "mem_63338"))
                    return 1;
                
                struct memblock mem_63341;
                
                mem_63341.references = NULL;
                if (memblock_alloc(ctx, &mem_63341, bytes_63336, "mem_63341"))
                    return 1;
                
                struct memblock mem_63344;
                
                mem_63344.references = NULL;
                if (memblock_alloc(ctx, &mem_63344, bytes_63336, "mem_63344"))
                    return 1;
                for (int32_t i_61223 = 0; i_61223 < sizze_58854; i_61223++) {
                    int32_t res_59000 = *(int32_t *) &xs_mem_63331.mem[i_61223 *
                                                                       4];
                    int32_t res_59001 = *(int32_t *) &xs_mem_63333.mem[i_61223 *
                                                                       4];
                    int32_t res_59002 = *(int32_t *) &xs_mem_63335.mem[i_61223 *
                                                                       4];
                    int32_t x_59003 = ashr32(i_61223, i_58981);
                    int32_t x_59004 = 2 & x_59003;
                    bool res_59005 = x_59004 == 0;
                    int32_t x_59006 = res_58995 & i_61223;
                    bool cond_59007 = x_59006 == 0;
                    int32_t res_59008;
                    int32_t res_59009;
                    int32_t res_59010;
                    
                    if (cond_59007) {
                        int32_t i_59011 = res_58995 | i_61223;
                        int32_t res_59012 =
                                *(int32_t *) &xs_mem_63331.mem[i_59011 * 4];
                        int32_t res_59013 =
                                *(int32_t *) &xs_mem_63333.mem[i_59011 * 4];
                        int32_t res_59014 =
                                *(int32_t *) &xs_mem_63335.mem[i_59011 * 4];
                        bool cond_59015 = res_59012 == res_59000;
                        bool res_59016 = sle32(res_59013, res_59001);
                        bool res_59017 = sle32(res_59012, res_59000);
                        bool x_59018 = cond_59015 && res_59016;
                        bool x_59019 = !cond_59015;
                        bool y_59020 = res_59017 && x_59019;
                        bool res_59021 = x_59018 || y_59020;
                        bool cond_59022 = res_59021 == res_59005;
                        int32_t res_59023;
                        
                        if (cond_59022) {
                            res_59023 = res_59012;
                        } else {
                            res_59023 = res_59000;
                        }
                        
                        int32_t res_59024;
                        
                        if (cond_59022) {
                            res_59024 = res_59013;
                        } else {
                            res_59024 = res_59001;
                        }
                        
                        int32_t res_59025;
                        
                        if (cond_59022) {
                            res_59025 = res_59014;
                        } else {
                            res_59025 = res_59002;
                        }
                        res_59008 = res_59023;
                        res_59009 = res_59024;
                        res_59010 = res_59025;
                    } else {
                        int32_t i_59026 = res_58995 ^ i_61223;
                        int32_t res_59027 =
                                *(int32_t *) &xs_mem_63331.mem[i_59026 * 4];
                        int32_t res_59028 =
                                *(int32_t *) &xs_mem_63333.mem[i_59026 * 4];
                        int32_t res_59029 =
                                *(int32_t *) &xs_mem_63335.mem[i_59026 * 4];
                        bool cond_59030 = res_59000 == res_59027;
                        bool res_59031 = sle32(res_59001, res_59028);
                        bool res_59032 = sle32(res_59000, res_59027);
                        bool x_59033 = cond_59030 && res_59031;
                        bool x_59034 = !cond_59030;
                        bool y_59035 = res_59032 && x_59034;
                        bool res_59036 = x_59033 || y_59035;
                        bool cond_59037 = res_59036 == res_59005;
                        int32_t res_59038;
                        
                        if (cond_59037) {
                            res_59038 = res_59027;
                        } else {
                            res_59038 = res_59000;
                        }
                        
                        int32_t res_59039;
                        
                        if (cond_59037) {
                            res_59039 = res_59028;
                        } else {
                            res_59039 = res_59001;
                        }
                        
                        int32_t res_59040;
                        
                        if (cond_59037) {
                            res_59040 = res_59029;
                        } else {
                            res_59040 = res_59002;
                        }
                        res_59008 = res_59038;
                        res_59009 = res_59039;
                        res_59010 = res_59040;
                    }
                    *(int32_t *) &mem_63338.mem[i_61223 * 4] = res_59008;
                    *(int32_t *) &mem_63341.mem[i_61223 * 4] = res_59009;
                    *(int32_t *) &mem_63344.mem[i_61223 * 4] = res_59010;
                }
                
                int64_t xs_mem_sizze_tmp_64386 = bytes_63336;
                struct memblock xs_mem_tmp_64387;
                
                xs_mem_tmp_64387.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64387, &mem_63338,
                                 "mem_63338") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_64388 = bytes_63336;
                struct memblock xs_mem_tmp_64389;
                
                xs_mem_tmp_64389.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64389, &mem_63341,
                                 "mem_63341") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_64390 = bytes_63336;
                struct memblock xs_mem_tmp_64391;
                
                xs_mem_tmp_64391.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64391, &mem_63344,
                                 "mem_63344") != 0)
                    return 1;
                xs_mem_sizze_63330 = xs_mem_sizze_tmp_64386;
                if (memblock_set(ctx, &xs_mem_63331, &xs_mem_tmp_64387,
                                 "xs_mem_tmp_64387") != 0)
                    return 1;
                xs_mem_sizze_63332 = xs_mem_sizze_tmp_64388;
                if (memblock_set(ctx, &xs_mem_63333, &xs_mem_tmp_64389,
                                 "xs_mem_tmp_64389") != 0)
                    return 1;
                xs_mem_sizze_63334 = xs_mem_sizze_tmp_64390;
                if (memblock_set(ctx, &xs_mem_63335, &xs_mem_tmp_64391,
                                 "xs_mem_tmp_64391") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64391,
                                   "xs_mem_tmp_64391") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64389,
                                   "xs_mem_tmp_64389") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64387,
                                   "xs_mem_tmp_64387") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63344, "mem_63344") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63341, "mem_63341") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63338, "mem_63338") != 0)
                    return 1;
            }
            res_mem_sizze_63357 = xs_mem_sizze_63330;
            if (memblock_set(ctx, &res_mem_63358, &xs_mem_63331,
                             "xs_mem_63331") != 0)
                return 1;
            res_mem_sizze_63359 = xs_mem_sizze_63332;
            if (memblock_set(ctx, &res_mem_63360, &xs_mem_63333,
                             "xs_mem_63333") != 0)
                return 1;
            res_mem_sizze_63361 = xs_mem_sizze_63334;
            if (memblock_set(ctx, &res_mem_63362, &xs_mem_63335,
                             "xs_mem_63335") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64377 = res_mem_sizze_63357;
            struct memblock xs_mem_tmp_64378;
            
            xs_mem_tmp_64378.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64378, &res_mem_63358,
                             "res_mem_63358") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64379 = res_mem_sizze_63359;
            struct memblock xs_mem_tmp_64380;
            
            xs_mem_tmp_64380.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64380, &res_mem_63360,
                             "res_mem_63360") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64381 = res_mem_sizze_63361;
            struct memblock xs_mem_tmp_64382;
            
            xs_mem_tmp_64382.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64382, &res_mem_63362,
                             "res_mem_63362") != 0)
                return 1;
            xs_mem_sizze_63324 = xs_mem_sizze_tmp_64377;
            if (memblock_set(ctx, &xs_mem_63325, &xs_mem_tmp_64378,
                             "xs_mem_tmp_64378") != 0)
                return 1;
            xs_mem_sizze_63326 = xs_mem_sizze_tmp_64379;
            if (memblock_set(ctx, &xs_mem_63327, &xs_mem_tmp_64380,
                             "xs_mem_tmp_64380") != 0)
                return 1;
            xs_mem_sizze_63328 = xs_mem_sizze_tmp_64381;
            if (memblock_set(ctx, &xs_mem_63329, &xs_mem_tmp_64382,
                             "xs_mem_tmp_64382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64382, "xs_mem_tmp_64382") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64380, "xs_mem_tmp_64380") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64378, "xs_mem_tmp_64378") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63335, "xs_mem_63335") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63333, "xs_mem_63333") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63331, "xs_mem_63331") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63362, "res_mem_63362") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63360, "res_mem_63360") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63358, "res_mem_63358") != 0)
                return 1;
        }
        indexed_mem_sizze_63363 = xs_mem_sizze_63324;
        if (memblock_set(ctx, &indexed_mem_63364, &xs_mem_63325,
                         "xs_mem_63325") != 0)
            return 1;
        indexed_mem_sizze_63365 = xs_mem_sizze_63326;
        if (memblock_set(ctx, &indexed_mem_63366, &xs_mem_63327,
                         "xs_mem_63327") != 0)
            return 1;
        indexed_mem_sizze_63367 = xs_mem_sizze_63328;
        if (memblock_set(ctx, &indexed_mem_63368, &xs_mem_63329,
                         "xs_mem_63329") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
            return 1;
        
        bool i_p_m_t_s_leq_w_59041 = slt32(m_58842, sizze_58854);
        bool y_59042 = zzero_leq_i_p_m_t_s_58843 && i_p_m_t_s_leq_w_59041;
        bool ok_or_empty_59044 = empty_slice_58841 || y_59042;
        bool index_certs_59045;
        
        if (!ok_or_empty_59044) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                                   "Index [", "", ":", last_offset_58826,
                                   "] out of bounds for array of shape [",
                                   sizze_58854, "].");
            if (memblock_unref(ctx, &xs_mem_63329, "xs_mem_63329") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63327, "xs_mem_63327") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63325, "xs_mem_63325") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63368, "indexed_mem_63368") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63366, "indexed_mem_63366") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63364, "indexed_mem_63364") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        struct memblock mem_63371;
        
        mem_63371.references = NULL;
        if (memblock_alloc(ctx, &mem_63371, bytes_63240, "mem_63371"))
            return 1;
        
        struct memblock mem_63373;
        
        mem_63373.references = NULL;
        if (memblock_alloc(ctx, &mem_63373, binop_x_63241, "mem_63373"))
            return 1;
        
        int32_t discard_61250;
        int32_t scanacc_61235 = 1;
        
        for (int32_t i_61241 = 0; i_61241 < last_offset_58826; i_61241++) {
            int32_t x_59067 = *(int32_t *) &indexed_mem_63364.mem[i_61241 * 4];
            int32_t x_59068 = *(int32_t *) &indexed_mem_63366.mem[i_61241 * 4];
            int32_t i_p_o_62192 = -1 + i_61241;
            int32_t rot_i_62193 = smod32(i_p_o_62192, last_offset_58826);
            int32_t x_59069 = *(int32_t *) &indexed_mem_63364.mem[rot_i_62193 *
                                                                  4];
            int32_t x_59070 = *(int32_t *) &indexed_mem_63366.mem[rot_i_62193 *
                                                                  4];
            int32_t x_59071 = *(int32_t *) &indexed_mem_63368.mem[i_61241 * 4];
            bool res_59072 = x_59067 == x_59069;
            bool res_59073 = x_59068 == x_59070;
            bool eq_59074 = res_59072 && res_59073;
            bool res_59075 = !eq_59074;
            int32_t res_59065;
            
            if (res_59075) {
                res_59065 = x_59071;
            } else {
                int32_t res_59066 = x_59071 * scanacc_61235;
                
                res_59065 = res_59066;
            }
            *(int32_t *) &mem_63371.mem[i_61241 * 4] = res_59065;
            *(bool *) &mem_63373.mem[i_61241] = res_59075;
            
            int32_t scanacc_tmp_64398 = res_59065;
            
            scanacc_61235 = scanacc_tmp_64398;
        }
        discard_61250 = scanacc_61235;
        if (memblock_unref(ctx, &indexed_mem_63364, "indexed_mem_63364") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63366, "indexed_mem_63366") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63368, "indexed_mem_63368") != 0)
            return 1;
        
        struct memblock mem_63384;
        
        mem_63384.references = NULL;
        if (memblock_alloc(ctx, &mem_63384, bytes_63240, "mem_63384"))
            return 1;
        
        int32_t discard_61256;
        int32_t scanacc_61252 = 0;
        
        for (int32_t i_61254 = 0; i_61254 < last_offset_58826; i_61254++) {
            int32_t i_p_o_62200 = 1 + i_61254;
            int32_t rot_i_62201 = smod32(i_p_o_62200, last_offset_58826);
            bool x_59081 = *(bool *) &mem_63373.mem[rot_i_62201];
            int32_t res_59082 = btoi_bool_i32(x_59081);
            int32_t res_59080 = res_59082 + scanacc_61252;
            
            *(int32_t *) &mem_63384.mem[i_61254 * 4] = res_59080;
            
            int32_t scanacc_tmp_64401 = res_59080;
            
            scanacc_61252 = scanacc_tmp_64401;
        }
        discard_61256 = scanacc_61252;
        
        int32_t res_59083;
        
        if (loop_cond_58853) {
            bool index_certs_59084;
            
            if (!zzero_leq_i_p_m_t_s_58843) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                       "Index [", m_58842,
                                       "] out of bounds for array of shape [",
                                       last_offset_58826, "].");
                if (memblock_unref(ctx, &mem_63384, "mem_63384") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63373, "mem_63373") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63371, "mem_63371") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63329, "xs_mem_63329") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63327, "xs_mem_63327") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63325, "xs_mem_63325") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63368,
                                   "indexed_mem_63368") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63366,
                                   "indexed_mem_63366") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63364,
                                   "indexed_mem_63364") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63167,
                                   "indexed_mem_63167") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63165,
                                   "indexed_mem_63165") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63163,
                                   "indexed_mem_63163") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int32_t res_59085 = *(int32_t *) &mem_63384.mem[m_58842 * 4];
            
            res_59083 = res_59085;
        } else {
            res_59083 = 0;
        }
        
        bool bounds_invalid_upwards_59086 = slt32(res_59083, 0);
        bool eq_x_zz_59087 = 0 == res_59083;
        bool not_p_59088 = !bounds_invalid_upwards_59086;
        bool p_and_eq_x_y_59089 = eq_x_zz_59087 && not_p_59088;
        bool dim_zzero_59090 = bounds_invalid_upwards_59086 ||
             p_and_eq_x_y_59089;
        bool both_empty_59091 = eq_x_zz_59087 && dim_zzero_59090;
        bool eq_x_y_59092 = res_59083 == 0;
        bool empty_or_match_59095 = not_p_59088 || both_empty_59091;
        bool empty_or_match_cert_59096;
        
        if (!empty_or_match_59095) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:24-29 -> tupleTest.fut:142:13-41 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                                   "Function return value does not match shape of type ",
                                   "*", "[", res_59083, "]", "t");
            if (memblock_unref(ctx, &mem_63384, "mem_63384") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63373, "mem_63373") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63371, "mem_63371") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63329, "xs_mem_63329") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63327, "xs_mem_63327") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63325, "xs_mem_63325") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63368, "indexed_mem_63368") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63366, "indexed_mem_63366") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63364, "indexed_mem_63364") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_63390 = sext_i32_i64(res_59083);
        int64_t bytes_63389 = 4 * binop_x_63390;
        struct memblock mem_63391;
        
        mem_63391.references = NULL;
        if (memblock_alloc(ctx, &mem_63391, bytes_63389, "mem_63391"))
            return 1;
        for (int32_t i_64403 = 0; i_64403 < res_59083; i_64403++) {
            *(int32_t *) &mem_63391.mem[i_64403 * 4] = 1;
        }
        for (int32_t write_iter_61257 = 0; write_iter_61257 < last_offset_58826;
             write_iter_61257++) {
            int32_t write_iv_61261 =
                    *(int32_t *) &mem_63384.mem[write_iter_61257 * 4];
            int32_t i_p_o_62205 = 1 + write_iter_61257;
            int32_t rot_i_62206 = smod32(i_p_o_62205, last_offset_58826);
            bool write_iv_61262 = *(bool *) &mem_63373.mem[rot_i_62206];
            int32_t res_59108;
            
            if (write_iv_61262) {
                int32_t res_59109 = write_iv_61261 - 1;
                
                res_59108 = res_59109;
            } else {
                res_59108 = -1;
            }
            
            bool less_than_zzero_61278 = slt32(res_59108, 0);
            bool greater_than_sizze_61279 = sle32(res_59083, res_59108);
            bool outside_bounds_dim_61280 = less_than_zzero_61278 ||
                 greater_than_sizze_61279;
            
            if (!outside_bounds_dim_61280) {
                memmove(mem_63391.mem + res_59108 * 4, mem_63371.mem +
                        write_iter_61257 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63371, "mem_63371") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63373, "mem_63373") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63384, "mem_63384") != 0)
            return 1;
        
        struct memblock mem_63398;
        
        mem_63398.references = NULL;
        if (memblock_alloc(ctx, &mem_63398, bytes_63389, "mem_63398"))
            return 1;
        
        struct memblock mem_63401;
        
        mem_63401.references = NULL;
        if (memblock_alloc(ctx, &mem_63401, bytes_63389, "mem_63401"))
            return 1;
        
        int32_t discard_61292;
        int32_t scanacc_61286 = 0;
        
        for (int32_t i_61289 = 0; i_61289 < res_59083; i_61289++) {
            int32_t x_59115 = *(int32_t *) &mem_63391.mem[i_61289 * 4];
            bool not_arg_59116 = x_59115 == 0;
            bool res_59117 = !not_arg_59116;
            int32_t part_res_59118;
            
            if (res_59117) {
                part_res_59118 = 0;
            } else {
                part_res_59118 = 1;
            }
            
            int32_t part_res_59119;
            
            if (res_59117) {
                part_res_59119 = 1;
            } else {
                part_res_59119 = 0;
            }
            
            int32_t zz_59114 = part_res_59119 + scanacc_61286;
            
            *(int32_t *) &mem_63398.mem[i_61289 * 4] = zz_59114;
            *(int32_t *) &mem_63401.mem[i_61289 * 4] = part_res_59118;
            
            int32_t scanacc_tmp_64405 = zz_59114;
            
            scanacc_61286 = scanacc_tmp_64405;
        }
        discard_61292 = scanacc_61286;
        
        int32_t last_index_59120 = res_59083 - 1;
        int32_t partition_sizze_59121;
        
        if (eq_x_y_59092) {
            partition_sizze_59121 = 0;
        } else {
            int32_t last_offset_59122 =
                    *(int32_t *) &mem_63398.mem[last_index_59120 * 4];
            
            partition_sizze_59121 = last_offset_59122;
        }
        
        int64_t binop_x_63411 = sext_i32_i64(partition_sizze_59121);
        int64_t bytes_63410 = 4 * binop_x_63411;
        struct memblock mem_63412;
        
        mem_63412.references = NULL;
        if (memblock_alloc(ctx, &mem_63412, bytes_63410, "mem_63412"))
            return 1;
        for (int32_t write_iter_61293 = 0; write_iter_61293 < res_59083;
             write_iter_61293++) {
            int32_t write_iv_61295 =
                    *(int32_t *) &mem_63401.mem[write_iter_61293 * 4];
            int32_t write_iv_61296 =
                    *(int32_t *) &mem_63398.mem[write_iter_61293 * 4];
            bool is_this_one_59130 = write_iv_61295 == 0;
            int32_t this_offset_59131 = -1 + write_iv_61296;
            int32_t total_res_59132;
            
            if (is_this_one_59130) {
                total_res_59132 = this_offset_59131;
            } else {
                total_res_59132 = -1;
            }
            
            bool less_than_zzero_61300 = slt32(total_res_59132, 0);
            bool greater_than_sizze_61301 = sle32(partition_sizze_59121,
                                                  total_res_59132);
            bool outside_bounds_dim_61302 = less_than_zzero_61300 ||
                 greater_than_sizze_61301;
            
            if (!outside_bounds_dim_61302) {
                memmove(mem_63412.mem + total_res_59132 * 4, mem_63391.mem +
                        write_iter_61293 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63391, "mem_63391") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63398, "mem_63398") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63401, "mem_63401") != 0)
            return 1;
        
        bool dim_eq_59133 = partition_sizze_59121 == last_offset_58826;
        bool arrays_equal_59134;
        
        if (dim_eq_59133) {
            bool all_equal_59136;
            bool redout_61306 = 1;
            
            for (int32_t i_61307 = 0; i_61307 < partition_sizze_59121;
                 i_61307++) {
                int32_t x_59140 = *(int32_t *) &mem_63412.mem[i_61307 * 4];
                int32_t y_59141 = *(int32_t *) &mem_63248.mem[i_61307 * 4];
                bool res_59142 = x_59140 == y_59141;
                bool res_59139 = res_59142 && redout_61306;
                bool redout_tmp_64409 = res_59139;
                
                redout_61306 = redout_tmp_64409;
            }
            all_equal_59136 = redout_61306;
            arrays_equal_59134 = all_equal_59136;
        } else {
            arrays_equal_59134 = 0;
        }
        if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63412, "mem_63412") != 0)
            return 1;
        cond_58811 = arrays_equal_59134;
        if (memblock_unref(ctx, &mem_63412, "mem_63412") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63401, "mem_63401") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63398, "mem_63398") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63391, "mem_63391") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63384, "mem_63384") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63373, "mem_63373") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63371, "mem_63371") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63329, "xs_mem_63329") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63327, "xs_mem_63327") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63325, "xs_mem_63325") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63368, "indexed_mem_63368") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63366, "indexed_mem_63366") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63364, "indexed_mem_63364") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63323, "res_mem_63323") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63321, "res_mem_63321") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63319, "res_mem_63319") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63275, "mem_63275") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63272, "mem_63272") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63269, "mem_63269") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63248, "mem_63248") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63245, "mem_63245") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63242, "mem_63242") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63231, "mem_63231") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63228, "mem_63228") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63223, "mem_63223") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63218, "mem_63218") != 0)
            return 1;
    } else {
        cond_58811 = 0;
    }
    
    bool cond_59143;
    
    if (cond_58811) {
        struct memblock mem_63419;
        
        mem_63419.references = NULL;
        if (memblock_alloc(ctx, &mem_63419, 24, "mem_63419"))
            return 1;
        
        struct memblock mem_63424;
        
        mem_63424.references = NULL;
        if (memblock_alloc(ctx, &mem_63424, 8, "mem_63424"))
            return 1;
        for (int32_t i_61310 = 0; i_61310 < 3; i_61310++) {
            for (int32_t i_64411 = 0; i_64411 < 2; i_64411++) {
                *(int32_t *) &mem_63424.mem[i_64411 * 4] = i_61310;
            }
            memmove(mem_63419.mem + 2 * i_61310 * 4, mem_63424.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
            return 1;
        
        struct memblock mem_63429;
        
        mem_63429.references = NULL;
        if (memblock_alloc(ctx, &mem_63429, 24, "mem_63429"))
            return 1;
        
        struct memblock mem_63432;
        
        mem_63432.references = NULL;
        if (memblock_alloc(ctx, &mem_63432, 24, "mem_63432"))
            return 1;
        
        int32_t discard_61320;
        int32_t scanacc_61314 = 0;
        
        for (int32_t i_61317 = 0; i_61317 < 6; i_61317++) {
            bool not_arg_59154 = i_61317 == 0;
            bool res_59155 = !not_arg_59154;
            int32_t part_res_59156;
            
            if (res_59155) {
                part_res_59156 = 0;
            } else {
                part_res_59156 = 1;
            }
            
            int32_t part_res_59157;
            
            if (res_59155) {
                part_res_59157 = 1;
            } else {
                part_res_59157 = 0;
            }
            
            int32_t zz_59152 = part_res_59157 + scanacc_61314;
            
            *(int32_t *) &mem_63429.mem[i_61317 * 4] = zz_59152;
            *(int32_t *) &mem_63432.mem[i_61317 * 4] = part_res_59156;
            
            int32_t scanacc_tmp_64412 = zz_59152;
            
            scanacc_61314 = scanacc_tmp_64412;
        }
        discard_61320 = scanacc_61314;
        
        int32_t last_offset_59158 = *(int32_t *) &mem_63429.mem[20];
        int64_t binop_x_63442 = sext_i32_i64(last_offset_59158);
        int64_t bytes_63441 = 4 * binop_x_63442;
        struct memblock mem_63443;
        
        mem_63443.references = NULL;
        if (memblock_alloc(ctx, &mem_63443, bytes_63441, "mem_63443"))
            return 1;
        
        struct memblock mem_63446;
        
        mem_63446.references = NULL;
        if (memblock_alloc(ctx, &mem_63446, bytes_63441, "mem_63446"))
            return 1;
        
        struct memblock mem_63449;
        
        mem_63449.references = NULL;
        if (memblock_alloc(ctx, &mem_63449, bytes_63441, "mem_63449"))
            return 1;
        for (int32_t write_iter_61321 = 0; write_iter_61321 < 6;
             write_iter_61321++) {
            int32_t write_iv_61325 =
                    *(int32_t *) &mem_63432.mem[write_iter_61321 * 4];
            int32_t write_iv_61326 =
                    *(int32_t *) &mem_63429.mem[write_iter_61321 * 4];
            int32_t new_index_62210 = squot32(write_iter_61321, 2);
            int32_t binop_y_62212 = 2 * new_index_62210;
            int32_t new_index_62213 = write_iter_61321 - binop_y_62212;
            bool is_this_one_59170 = write_iv_61325 == 0;
            int32_t this_offset_59171 = -1 + write_iv_61326;
            int32_t total_res_59172;
            
            if (is_this_one_59170) {
                total_res_59172 = this_offset_59171;
            } else {
                total_res_59172 = -1;
            }
            
            bool less_than_zzero_61330 = slt32(total_res_59172, 0);
            bool greater_than_sizze_61331 = sle32(last_offset_59158,
                                                  total_res_59172);
            bool outside_bounds_dim_61332 = less_than_zzero_61330 ||
                 greater_than_sizze_61331;
            
            if (!outside_bounds_dim_61332) {
                memmove(mem_63443.mem + total_res_59172 * 4, mem_63419.mem +
                        (2 * new_index_62210 + new_index_62213) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_61332) {
                struct memblock mem_63458;
                
                mem_63458.references = NULL;
                if (memblock_alloc(ctx, &mem_63458, 4, "mem_63458"))
                    return 1;
                
                int32_t x_64419;
                
                for (int32_t i_64418 = 0; i_64418 < 1; i_64418++) {
                    x_64419 = new_index_62213 + sext_i32_i32(i_64418);
                    *(int32_t *) &mem_63458.mem[i_64418 * 4] = x_64419;
                }
                memmove(mem_63446.mem + total_res_59172 * 4, mem_63458.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63458, "mem_63458") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63458, "mem_63458") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_61332) {
                struct memblock mem_63461;
                
                mem_63461.references = NULL;
                if (memblock_alloc(ctx, &mem_63461, 4, "mem_63461"))
                    return 1;
                
                int32_t x_64421;
                
                for (int32_t i_64420 = 0; i_64420 < 1; i_64420++) {
                    x_64421 = write_iter_61321 + sext_i32_i32(i_64420);
                    *(int32_t *) &mem_63461.mem[i_64420 * 4] = x_64421;
                }
                memmove(mem_63449.mem + total_res_59172 * 4, mem_63461.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63461, "mem_63461") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63461, "mem_63461") != 0)
                    return 1;
            }
        }
        if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
            return 1;
        
        int32_t conc_tmp_59182 = last_offset_59158 + last_offset_59158;
        int64_t binop_x_63469 = sext_i32_i64(conc_tmp_59182);
        int64_t bytes_63468 = 4 * binop_x_63469;
        struct memblock mem_63470;
        
        mem_63470.references = NULL;
        if (memblock_alloc(ctx, &mem_63470, bytes_63468, "mem_63470"))
            return 1;
        
        int32_t tmp_offs_64422 = 0;
        
        memmove(mem_63470.mem + tmp_offs_64422 * 4, mem_63443.mem + 0,
                last_offset_59158 * sizeof(int32_t));
        tmp_offs_64422 += last_offset_59158;
        memmove(mem_63470.mem + tmp_offs_64422 * 4, mem_63443.mem + 0,
                last_offset_59158 * sizeof(int32_t));
        tmp_offs_64422 += last_offset_59158;
        
        struct memblock mem_63473;
        
        mem_63473.references = NULL;
        if (memblock_alloc(ctx, &mem_63473, bytes_63468, "mem_63473"))
            return 1;
        
        int32_t tmp_offs_64423 = 0;
        
        memmove(mem_63473.mem + tmp_offs_64423 * 4, mem_63446.mem + 0,
                last_offset_59158 * sizeof(int32_t));
        tmp_offs_64423 += last_offset_59158;
        memmove(mem_63473.mem + tmp_offs_64423 * 4, mem_63446.mem + 0,
                last_offset_59158 * sizeof(int32_t));
        tmp_offs_64423 += last_offset_59158;
        
        struct memblock mem_63476;
        
        mem_63476.references = NULL;
        if (memblock_alloc(ctx, &mem_63476, bytes_63468, "mem_63476"))
            return 1;
        
        int32_t tmp_offs_64424 = 0;
        
        memmove(mem_63476.mem + tmp_offs_64424 * 4, mem_63449.mem + 0,
                last_offset_59158 * sizeof(int32_t));
        tmp_offs_64424 += last_offset_59158;
        memmove(mem_63476.mem + tmp_offs_64424 * 4, mem_63449.mem + 0,
                last_offset_59158 * sizeof(int32_t));
        tmp_offs_64424 += last_offset_59158;
        
        bool cond_59186 = conc_tmp_59182 == 0;
        bool loop_cond_59187 = slt32(1, conc_tmp_59182);
        int32_t sizze_59188;
        int32_t sizze_59189;
        int32_t sizze_59190;
        int64_t res_mem_sizze_63519;
        struct memblock res_mem_63520;
        
        res_mem_63520.references = NULL;
        
        int64_t res_mem_sizze_63521;
        struct memblock res_mem_63522;
        
        res_mem_63522.references = NULL;
        
        int64_t res_mem_sizze_63523;
        struct memblock res_mem_63524;
        
        res_mem_63524.references = NULL;
        
        int32_t res_59194;
        
        if (cond_59186) {
            struct memblock mem_63479;
            
            mem_63479.references = NULL;
            if (memblock_alloc(ctx, &mem_63479, bytes_63468, "mem_63479"))
                return 1;
            memmove(mem_63479.mem + 0, mem_63470.mem + 0, conc_tmp_59182 *
                    sizeof(int32_t));
            
            struct memblock mem_63482;
            
            mem_63482.references = NULL;
            if (memblock_alloc(ctx, &mem_63482, bytes_63468, "mem_63482"))
                return 1;
            memmove(mem_63482.mem + 0, mem_63473.mem + 0, conc_tmp_59182 *
                    sizeof(int32_t));
            
            struct memblock mem_63485;
            
            mem_63485.references = NULL;
            if (memblock_alloc(ctx, &mem_63485, bytes_63468, "mem_63485"))
                return 1;
            memmove(mem_63485.mem + 0, mem_63476.mem + 0, conc_tmp_59182 *
                    sizeof(int32_t));
            sizze_59188 = conc_tmp_59182;
            sizze_59189 = conc_tmp_59182;
            sizze_59190 = conc_tmp_59182;
            res_mem_sizze_63519 = bytes_63468;
            if (memblock_set(ctx, &res_mem_63520, &mem_63479, "mem_63479") != 0)
                return 1;
            res_mem_sizze_63521 = bytes_63468;
            if (memblock_set(ctx, &res_mem_63522, &mem_63482, "mem_63482") != 0)
                return 1;
            res_mem_sizze_63523 = bytes_63468;
            if (memblock_set(ctx, &res_mem_63524, &mem_63485, "mem_63485") != 0)
                return 1;
            res_59194 = 0;
            if (memblock_unref(ctx, &mem_63485, "mem_63485") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63482, "mem_63482") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63479, "mem_63479") != 0)
                return 1;
        } else {
            bool res_59206;
            int32_t res_59207;
            int32_t res_59208;
            bool loop_while_59209;
            int32_t r_59210;
            int32_t n_59211;
            
            loop_while_59209 = loop_cond_59187;
            r_59210 = 0;
            n_59211 = conc_tmp_59182;
            while (loop_while_59209) {
                int32_t res_59212 = sdiv32(n_59211, 2);
                int32_t res_59213 = 1 + r_59210;
                bool loop_cond_59214 = slt32(1, res_59212);
                bool loop_while_tmp_64425 = loop_cond_59214;
                int32_t r_tmp_64426 = res_59213;
                int32_t n_tmp_64427;
                
                n_tmp_64427 = res_59212;
                loop_while_59209 = loop_while_tmp_64425;
                r_59210 = r_tmp_64426;
                n_59211 = n_tmp_64427;
            }
            res_59206 = loop_while_59209;
            res_59207 = r_59210;
            res_59208 = n_59211;
            
            int32_t y_59215 = 1 << res_59207;
            bool cond_59216 = conc_tmp_59182 == y_59215;
            int32_t y_59217 = 1 + res_59207;
            int32_t x_59218 = 1 << y_59217;
            int32_t arg_59219 = x_59218 - conc_tmp_59182;
            bool bounds_invalid_upwards_59220 = slt32(arg_59219, 0);
            int32_t conc_tmp_59221 = conc_tmp_59182 + arg_59219;
            int32_t sizze_59222;
            
            if (cond_59216) {
                sizze_59222 = conc_tmp_59182;
            } else {
                sizze_59222 = conc_tmp_59221;
            }
            
            int32_t res_59223;
            
            if (cond_59216) {
                res_59223 = res_59207;
            } else {
                res_59223 = y_59217;
            }
            
            int64_t binop_x_63505 = sext_i32_i64(conc_tmp_59221);
            int64_t bytes_63504 = 4 * binop_x_63505;
            int64_t res_mem_sizze_63513;
            struct memblock res_mem_63514;
            
            res_mem_63514.references = NULL;
            
            int64_t res_mem_sizze_63515;
            struct memblock res_mem_63516;
            
            res_mem_63516.references = NULL;
            
            int64_t res_mem_sizze_63517;
            struct memblock res_mem_63518;
            
            res_mem_63518.references = NULL;
            if (cond_59216) {
                struct memblock mem_63488;
                
                mem_63488.references = NULL;
                if (memblock_alloc(ctx, &mem_63488, bytes_63468, "mem_63488"))
                    return 1;
                memmove(mem_63488.mem + 0, mem_63470.mem + 0, conc_tmp_59182 *
                        sizeof(int32_t));
                
                struct memblock mem_63491;
                
                mem_63491.references = NULL;
                if (memblock_alloc(ctx, &mem_63491, bytes_63468, "mem_63491"))
                    return 1;
                memmove(mem_63491.mem + 0, mem_63473.mem + 0, conc_tmp_59182 *
                        sizeof(int32_t));
                
                struct memblock mem_63494;
                
                mem_63494.references = NULL;
                if (memblock_alloc(ctx, &mem_63494, bytes_63468, "mem_63494"))
                    return 1;
                memmove(mem_63494.mem + 0, mem_63476.mem + 0, conc_tmp_59182 *
                        sizeof(int32_t));
                res_mem_sizze_63513 = bytes_63468;
                if (memblock_set(ctx, &res_mem_63514, &mem_63488,
                                 "mem_63488") != 0)
                    return 1;
                res_mem_sizze_63515 = bytes_63468;
                if (memblock_set(ctx, &res_mem_63516, &mem_63491,
                                 "mem_63491") != 0)
                    return 1;
                res_mem_sizze_63517 = bytes_63468;
                if (memblock_set(ctx, &res_mem_63518, &mem_63494,
                                 "mem_63494") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63494, "mem_63494") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63491, "mem_63491") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63488, "mem_63488") != 0)
                    return 1;
            } else {
                bool y_59241 = slt32(0, conc_tmp_59182);
                bool index_certs_59242;
                
                if (!y_59241) {
                    ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                           "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                           "Index [", 0,
                                           "] out of bounds for array of shape [",
                                           conc_tmp_59182, "].");
                    if (memblock_unref(ctx, &res_mem_63518, "res_mem_63518") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63516, "res_mem_63516") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63514, "res_mem_63514") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63167,
                                       "indexed_mem_63167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63165,
                                       "indexed_mem_63165") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63163,
                                       "indexed_mem_63163") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                        return 1;
                    return 1;
                }
                
                bool index_concat_cmp_59243 = sle32(last_offset_59158, 0);
                int32_t index_concat_branch_59244;
                
                if (index_concat_cmp_59243) {
                    int32_t index_concat_i_59245 = 0 - last_offset_59158;
                    int32_t index_concat_59246 =
                            *(int32_t *) &mem_63443.mem[index_concat_i_59245 *
                                                        4];
                    
                    index_concat_branch_59244 = index_concat_59246;
                } else {
                    int32_t index_concat_59247 = *(int32_t *) &mem_63443.mem[0];
                    
                    index_concat_branch_59244 = index_concat_59247;
                }
                
                int32_t index_concat_branch_59248;
                
                if (index_concat_cmp_59243) {
                    int32_t index_concat_i_59249 = 0 - last_offset_59158;
                    int32_t index_concat_59250 =
                            *(int32_t *) &mem_63446.mem[index_concat_i_59249 *
                                                        4];
                    
                    index_concat_branch_59248 = index_concat_59250;
                } else {
                    int32_t index_concat_59251 = *(int32_t *) &mem_63446.mem[0];
                    
                    index_concat_branch_59248 = index_concat_59251;
                }
                
                int32_t index_concat_branch_59252;
                
                if (index_concat_cmp_59243) {
                    int32_t index_concat_i_59253 = 0 - last_offset_59158;
                    int32_t index_concat_59254 =
                            *(int32_t *) &mem_63449.mem[index_concat_i_59253 *
                                                        4];
                    
                    index_concat_branch_59252 = index_concat_59254;
                } else {
                    int32_t index_concat_59255 = *(int32_t *) &mem_63449.mem[0];
                    
                    index_concat_branch_59252 = index_concat_59255;
                }
                
                int32_t res_59256;
                int32_t res_59257;
                int32_t res_59258;
                int32_t redout_61348;
                int32_t redout_61349;
                int32_t redout_61350;
                
                redout_61348 = index_concat_branch_59244;
                redout_61349 = index_concat_branch_59248;
                redout_61350 = index_concat_branch_59252;
                for (int32_t i_61351 = 0; i_61351 < conc_tmp_59182; i_61351++) {
                    bool index_concat_cmp_62240 = sle32(last_offset_59158,
                                                        i_61351);
                    int32_t index_concat_branch_62244;
                    
                    if (index_concat_cmp_62240) {
                        int32_t index_concat_i_62241 = i_61351 -
                                last_offset_59158;
                        int32_t index_concat_62242 =
                                *(int32_t *) &mem_63443.mem[index_concat_i_62241 *
                                                            4];
                        
                        index_concat_branch_62244 = index_concat_62242;
                    } else {
                        int32_t index_concat_62243 =
                                *(int32_t *) &mem_63443.mem[i_61351 * 4];
                        
                        index_concat_branch_62244 = index_concat_62243;
                    }
                    
                    int32_t index_concat_branch_62238;
                    
                    if (index_concat_cmp_62240) {
                        int32_t index_concat_i_62235 = i_61351 -
                                last_offset_59158;
                        int32_t index_concat_62236 =
                                *(int32_t *) &mem_63446.mem[index_concat_i_62235 *
                                                            4];
                        
                        index_concat_branch_62238 = index_concat_62236;
                    } else {
                        int32_t index_concat_62237 =
                                *(int32_t *) &mem_63446.mem[i_61351 * 4];
                        
                        index_concat_branch_62238 = index_concat_62237;
                    }
                    
                    int32_t index_concat_branch_62232;
                    
                    if (index_concat_cmp_62240) {
                        int32_t index_concat_i_62229 = i_61351 -
                                last_offset_59158;
                        int32_t index_concat_62230 =
                                *(int32_t *) &mem_63449.mem[index_concat_i_62229 *
                                                            4];
                        
                        index_concat_branch_62232 = index_concat_62230;
                    } else {
                        int32_t index_concat_62231 =
                                *(int32_t *) &mem_63449.mem[i_61351 * 4];
                        
                        index_concat_branch_62232 = index_concat_62231;
                    }
                    
                    bool cond_59265 = redout_61348 == index_concat_branch_62244;
                    bool res_59266 = sle32(redout_61349,
                                           index_concat_branch_62238);
                    bool res_59267 = sle32(redout_61348,
                                           index_concat_branch_62244);
                    bool x_59268 = cond_59265 && res_59266;
                    bool x_59269 = !cond_59265;
                    bool y_59270 = res_59267 && x_59269;
                    bool res_59271 = x_59268 || y_59270;
                    int32_t res_59272;
                    
                    if (res_59271) {
                        res_59272 = index_concat_branch_62244;
                    } else {
                        res_59272 = redout_61348;
                    }
                    
                    int32_t res_59273;
                    
                    if (res_59271) {
                        res_59273 = index_concat_branch_62238;
                    } else {
                        res_59273 = redout_61349;
                    }
                    
                    int32_t res_59274;
                    
                    if (res_59271) {
                        res_59274 = index_concat_branch_62232;
                    } else {
                        res_59274 = redout_61350;
                    }
                    
                    int32_t redout_tmp_64428 = res_59272;
                    int32_t redout_tmp_64429 = res_59273;
                    int32_t redout_tmp_64430;
                    
                    redout_tmp_64430 = res_59274;
                    redout_61348 = redout_tmp_64428;
                    redout_61349 = redout_tmp_64429;
                    redout_61350 = redout_tmp_64430;
                }
                res_59256 = redout_61348;
                res_59257 = redout_61349;
                res_59258 = redout_61350;
                
                bool eq_x_zz_59278 = 0 == arg_59219;
                bool not_p_59279 = !bounds_invalid_upwards_59220;
                bool p_and_eq_x_y_59280 = eq_x_zz_59278 && not_p_59279;
                bool dim_zzero_59281 = bounds_invalid_upwards_59220 ||
                     p_and_eq_x_y_59280;
                bool both_empty_59282 = eq_x_zz_59278 && dim_zzero_59281;
                bool eq_x_y_59283 = arg_59219 == 0;
                bool p_and_eq_x_y_59284 = bounds_invalid_upwards_59220 &&
                     eq_x_y_59283;
                bool dim_match_59285 = not_p_59279 || p_and_eq_x_y_59284;
                bool empty_or_match_59286 = both_empty_59282 || dim_match_59285;
                bool empty_or_match_cert_59287;
                
                if (!empty_or_match_59286) {
                    ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                           "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                           "Function return value does not match shape of type ",
                                           "*", "[", arg_59219, "]", "t");
                    if (memblock_unref(ctx, &res_mem_63518, "res_mem_63518") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63516, "res_mem_63516") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63514, "res_mem_63514") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63167,
                                       "indexed_mem_63167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63165,
                                       "indexed_mem_63165") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63163,
                                       "indexed_mem_63163") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                        return 1;
                    return 1;
                }
                
                int64_t binop_x_63496 = sext_i32_i64(arg_59219);
                int64_t bytes_63495 = 4 * binop_x_63496;
                struct memblock mem_63497;
                
                mem_63497.references = NULL;
                if (memblock_alloc(ctx, &mem_63497, bytes_63495, "mem_63497"))
                    return 1;
                for (int32_t i_64431 = 0; i_64431 < arg_59219; i_64431++) {
                    *(int32_t *) &mem_63497.mem[i_64431 * 4] = res_59256;
                }
                
                struct memblock mem_63500;
                
                mem_63500.references = NULL;
                if (memblock_alloc(ctx, &mem_63500, bytes_63495, "mem_63500"))
                    return 1;
                for (int32_t i_64432 = 0; i_64432 < arg_59219; i_64432++) {
                    *(int32_t *) &mem_63500.mem[i_64432 * 4] = res_59257;
                }
                
                struct memblock mem_63503;
                
                mem_63503.references = NULL;
                if (memblock_alloc(ctx, &mem_63503, bytes_63495, "mem_63503"))
                    return 1;
                for (int32_t i_64433 = 0; i_64433 < arg_59219; i_64433++) {
                    *(int32_t *) &mem_63503.mem[i_64433 * 4] = res_59258;
                }
                
                struct memblock mem_63506;
                
                mem_63506.references = NULL;
                if (memblock_alloc(ctx, &mem_63506, bytes_63504, "mem_63506"))
                    return 1;
                
                int32_t tmp_offs_64434 = 0;
                
                memmove(mem_63506.mem + tmp_offs_64434 * 4, mem_63443.mem + 0,
                        last_offset_59158 * sizeof(int32_t));
                tmp_offs_64434 += last_offset_59158;
                memmove(mem_63506.mem + tmp_offs_64434 * 4, mem_63443.mem + 0,
                        last_offset_59158 * sizeof(int32_t));
                tmp_offs_64434 += last_offset_59158;
                memmove(mem_63506.mem + tmp_offs_64434 * 4, mem_63497.mem + 0,
                        arg_59219 * sizeof(int32_t));
                tmp_offs_64434 += arg_59219;
                if (memblock_unref(ctx, &mem_63497, "mem_63497") != 0)
                    return 1;
                
                struct memblock mem_63509;
                
                mem_63509.references = NULL;
                if (memblock_alloc(ctx, &mem_63509, bytes_63504, "mem_63509"))
                    return 1;
                
                int32_t tmp_offs_64435 = 0;
                
                memmove(mem_63509.mem + tmp_offs_64435 * 4, mem_63446.mem + 0,
                        last_offset_59158 * sizeof(int32_t));
                tmp_offs_64435 += last_offset_59158;
                memmove(mem_63509.mem + tmp_offs_64435 * 4, mem_63446.mem + 0,
                        last_offset_59158 * sizeof(int32_t));
                tmp_offs_64435 += last_offset_59158;
                memmove(mem_63509.mem + tmp_offs_64435 * 4, mem_63500.mem + 0,
                        arg_59219 * sizeof(int32_t));
                tmp_offs_64435 += arg_59219;
                if (memblock_unref(ctx, &mem_63500, "mem_63500") != 0)
                    return 1;
                
                struct memblock mem_63512;
                
                mem_63512.references = NULL;
                if (memblock_alloc(ctx, &mem_63512, bytes_63504, "mem_63512"))
                    return 1;
                
                int32_t tmp_offs_64436 = 0;
                
                memmove(mem_63512.mem + tmp_offs_64436 * 4, mem_63449.mem + 0,
                        last_offset_59158 * sizeof(int32_t));
                tmp_offs_64436 += last_offset_59158;
                memmove(mem_63512.mem + tmp_offs_64436 * 4, mem_63449.mem + 0,
                        last_offset_59158 * sizeof(int32_t));
                tmp_offs_64436 += last_offset_59158;
                memmove(mem_63512.mem + tmp_offs_64436 * 4, mem_63503.mem + 0,
                        arg_59219 * sizeof(int32_t));
                tmp_offs_64436 += arg_59219;
                if (memblock_unref(ctx, &mem_63503, "mem_63503") != 0)
                    return 1;
                res_mem_sizze_63513 = bytes_63504;
                if (memblock_set(ctx, &res_mem_63514, &mem_63506,
                                 "mem_63506") != 0)
                    return 1;
                res_mem_sizze_63515 = bytes_63504;
                if (memblock_set(ctx, &res_mem_63516, &mem_63509,
                                 "mem_63509") != 0)
                    return 1;
                res_mem_sizze_63517 = bytes_63504;
                if (memblock_set(ctx, &res_mem_63518, &mem_63512,
                                 "mem_63512") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63512, "mem_63512") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63509, "mem_63509") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63506, "mem_63506") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63503, "mem_63503") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63500, "mem_63500") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63497, "mem_63497") != 0)
                    return 1;
            }
            sizze_59188 = sizze_59222;
            sizze_59189 = sizze_59222;
            sizze_59190 = sizze_59222;
            res_mem_sizze_63519 = res_mem_sizze_63513;
            if (memblock_set(ctx, &res_mem_63520, &res_mem_63514,
                             "res_mem_63514") != 0)
                return 1;
            res_mem_sizze_63521 = res_mem_sizze_63515;
            if (memblock_set(ctx, &res_mem_63522, &res_mem_63516,
                             "res_mem_63516") != 0)
                return 1;
            res_mem_sizze_63523 = res_mem_sizze_63517;
            if (memblock_set(ctx, &res_mem_63524, &res_mem_63518,
                             "res_mem_63518") != 0)
                return 1;
            res_59194 = res_59223;
            if (memblock_unref(ctx, &res_mem_63518, "res_mem_63518") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63516, "res_mem_63516") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63514, "res_mem_63514") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
            return 1;
        
        bool dim_zzero_59297 = 0 == sizze_59189;
        bool dim_zzero_59298 = 0 == sizze_59188;
        bool both_empty_59299 = dim_zzero_59297 && dim_zzero_59298;
        bool dim_match_59300 = sizze_59188 == sizze_59189;
        bool empty_or_match_59301 = both_empty_59299 || dim_match_59300;
        bool empty_or_match_cert_59302;
        
        if (!empty_or_match_59301) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        bool dim_zzero_59304 = 0 == sizze_59190;
        bool both_empty_59305 = dim_zzero_59298 && dim_zzero_59304;
        bool dim_match_59306 = sizze_59188 == sizze_59190;
        bool empty_or_match_59307 = both_empty_59305 || dim_match_59306;
        bool empty_or_match_cert_59308;
        
        if (!empty_or_match_59307) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_63538 = sext_i32_i64(sizze_59188);
        int64_t bytes_63537 = 4 * binop_x_63538;
        int64_t indexed_mem_sizze_63564;
        struct memblock indexed_mem_63565;
        
        indexed_mem_63565.references = NULL;
        
        int64_t indexed_mem_sizze_63566;
        struct memblock indexed_mem_63567;
        
        indexed_mem_63567.references = NULL;
        
        int64_t indexed_mem_sizze_63568;
        struct memblock indexed_mem_63569;
        
        indexed_mem_63569.references = NULL;
        
        int64_t xs_mem_sizze_63525;
        struct memblock xs_mem_63526;
        
        xs_mem_63526.references = NULL;
        
        int64_t xs_mem_sizze_63527;
        struct memblock xs_mem_63528;
        
        xs_mem_63528.references = NULL;
        
        int64_t xs_mem_sizze_63529;
        struct memblock xs_mem_63530;
        
        xs_mem_63530.references = NULL;
        xs_mem_sizze_63525 = res_mem_sizze_63519;
        if (memblock_set(ctx, &xs_mem_63526, &res_mem_63520, "res_mem_63520") !=
            0)
            return 1;
        xs_mem_sizze_63527 = res_mem_sizze_63521;
        if (memblock_set(ctx, &xs_mem_63528, &res_mem_63522, "res_mem_63522") !=
            0)
            return 1;
        xs_mem_sizze_63529 = res_mem_sizze_63523;
        if (memblock_set(ctx, &xs_mem_63530, &res_mem_63524, "res_mem_63524") !=
            0)
            return 1;
        for (int32_t i_59325 = 0; i_59325 < res_59194; i_59325++) {
            int32_t upper_bound_59326 = 1 + i_59325;
            int64_t res_mem_sizze_63558;
            struct memblock res_mem_63559;
            
            res_mem_63559.references = NULL;
            
            int64_t res_mem_sizze_63560;
            struct memblock res_mem_63561;
            
            res_mem_63561.references = NULL;
            
            int64_t res_mem_sizze_63562;
            struct memblock res_mem_63563;
            
            res_mem_63563.references = NULL;
            
            int64_t xs_mem_sizze_63531;
            struct memblock xs_mem_63532;
            
            xs_mem_63532.references = NULL;
            
            int64_t xs_mem_sizze_63533;
            struct memblock xs_mem_63534;
            
            xs_mem_63534.references = NULL;
            
            int64_t xs_mem_sizze_63535;
            struct memblock xs_mem_63536;
            
            xs_mem_63536.references = NULL;
            xs_mem_sizze_63531 = xs_mem_sizze_63525;
            if (memblock_set(ctx, &xs_mem_63532, &xs_mem_63526,
                             "xs_mem_63526") != 0)
                return 1;
            xs_mem_sizze_63533 = xs_mem_sizze_63527;
            if (memblock_set(ctx, &xs_mem_63534, &xs_mem_63528,
                             "xs_mem_63528") != 0)
                return 1;
            xs_mem_sizze_63535 = xs_mem_sizze_63529;
            if (memblock_set(ctx, &xs_mem_63536, &xs_mem_63530,
                             "xs_mem_63530") != 0)
                return 1;
            for (int32_t j_59337 = 0; j_59337 < upper_bound_59326; j_59337++) {
                int32_t y_59338 = i_59325 - j_59337;
                int32_t res_59339 = 1 << y_59338;
                struct memblock mem_63539;
                
                mem_63539.references = NULL;
                if (memblock_alloc(ctx, &mem_63539, bytes_63537, "mem_63539"))
                    return 1;
                
                struct memblock mem_63542;
                
                mem_63542.references = NULL;
                if (memblock_alloc(ctx, &mem_63542, bytes_63537, "mem_63542"))
                    return 1;
                
                struct memblock mem_63545;
                
                mem_63545.references = NULL;
                if (memblock_alloc(ctx, &mem_63545, bytes_63537, "mem_63545"))
                    return 1;
                for (int32_t i_61358 = 0; i_61358 < sizze_59188; i_61358++) {
                    int32_t res_59344 = *(int32_t *) &xs_mem_63532.mem[i_61358 *
                                                                       4];
                    int32_t res_59345 = *(int32_t *) &xs_mem_63534.mem[i_61358 *
                                                                       4];
                    int32_t res_59346 = *(int32_t *) &xs_mem_63536.mem[i_61358 *
                                                                       4];
                    int32_t x_59347 = ashr32(i_61358, i_59325);
                    int32_t x_59348 = 2 & x_59347;
                    bool res_59349 = x_59348 == 0;
                    int32_t x_59350 = res_59339 & i_61358;
                    bool cond_59351 = x_59350 == 0;
                    int32_t res_59352;
                    int32_t res_59353;
                    int32_t res_59354;
                    
                    if (cond_59351) {
                        int32_t i_59355 = res_59339 | i_61358;
                        int32_t res_59356 =
                                *(int32_t *) &xs_mem_63532.mem[i_59355 * 4];
                        int32_t res_59357 =
                                *(int32_t *) &xs_mem_63534.mem[i_59355 * 4];
                        int32_t res_59358 =
                                *(int32_t *) &xs_mem_63536.mem[i_59355 * 4];
                        bool cond_59359 = res_59356 == res_59344;
                        bool res_59360 = sle32(res_59357, res_59345);
                        bool res_59361 = sle32(res_59356, res_59344);
                        bool x_59362 = cond_59359 && res_59360;
                        bool x_59363 = !cond_59359;
                        bool y_59364 = res_59361 && x_59363;
                        bool res_59365 = x_59362 || y_59364;
                        bool cond_59366 = res_59365 == res_59349;
                        int32_t res_59367;
                        
                        if (cond_59366) {
                            res_59367 = res_59356;
                        } else {
                            res_59367 = res_59344;
                        }
                        
                        int32_t res_59368;
                        
                        if (cond_59366) {
                            res_59368 = res_59357;
                        } else {
                            res_59368 = res_59345;
                        }
                        
                        int32_t res_59369;
                        
                        if (cond_59366) {
                            res_59369 = res_59358;
                        } else {
                            res_59369 = res_59346;
                        }
                        res_59352 = res_59367;
                        res_59353 = res_59368;
                        res_59354 = res_59369;
                    } else {
                        int32_t i_59370 = res_59339 ^ i_61358;
                        int32_t res_59371 =
                                *(int32_t *) &xs_mem_63532.mem[i_59370 * 4];
                        int32_t res_59372 =
                                *(int32_t *) &xs_mem_63534.mem[i_59370 * 4];
                        int32_t res_59373 =
                                *(int32_t *) &xs_mem_63536.mem[i_59370 * 4];
                        bool cond_59374 = res_59344 == res_59371;
                        bool res_59375 = sle32(res_59345, res_59372);
                        bool res_59376 = sle32(res_59344, res_59371);
                        bool x_59377 = cond_59374 && res_59375;
                        bool x_59378 = !cond_59374;
                        bool y_59379 = res_59376 && x_59378;
                        bool res_59380 = x_59377 || y_59379;
                        bool cond_59381 = res_59380 == res_59349;
                        int32_t res_59382;
                        
                        if (cond_59381) {
                            res_59382 = res_59371;
                        } else {
                            res_59382 = res_59344;
                        }
                        
                        int32_t res_59383;
                        
                        if (cond_59381) {
                            res_59383 = res_59372;
                        } else {
                            res_59383 = res_59345;
                        }
                        
                        int32_t res_59384;
                        
                        if (cond_59381) {
                            res_59384 = res_59373;
                        } else {
                            res_59384 = res_59346;
                        }
                        res_59352 = res_59382;
                        res_59353 = res_59383;
                        res_59354 = res_59384;
                    }
                    *(int32_t *) &mem_63539.mem[i_61358 * 4] = res_59352;
                    *(int32_t *) &mem_63542.mem[i_61358 * 4] = res_59353;
                    *(int32_t *) &mem_63545.mem[i_61358 * 4] = res_59354;
                }
                
                int64_t xs_mem_sizze_tmp_64446 = bytes_63537;
                struct memblock xs_mem_tmp_64447;
                
                xs_mem_tmp_64447.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64447, &mem_63539,
                                 "mem_63539") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_64448 = bytes_63537;
                struct memblock xs_mem_tmp_64449;
                
                xs_mem_tmp_64449.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64449, &mem_63542,
                                 "mem_63542") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_64450 = bytes_63537;
                struct memblock xs_mem_tmp_64451;
                
                xs_mem_tmp_64451.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64451, &mem_63545,
                                 "mem_63545") != 0)
                    return 1;
                xs_mem_sizze_63531 = xs_mem_sizze_tmp_64446;
                if (memblock_set(ctx, &xs_mem_63532, &xs_mem_tmp_64447,
                                 "xs_mem_tmp_64447") != 0)
                    return 1;
                xs_mem_sizze_63533 = xs_mem_sizze_tmp_64448;
                if (memblock_set(ctx, &xs_mem_63534, &xs_mem_tmp_64449,
                                 "xs_mem_tmp_64449") != 0)
                    return 1;
                xs_mem_sizze_63535 = xs_mem_sizze_tmp_64450;
                if (memblock_set(ctx, &xs_mem_63536, &xs_mem_tmp_64451,
                                 "xs_mem_tmp_64451") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64451,
                                   "xs_mem_tmp_64451") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64449,
                                   "xs_mem_tmp_64449") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64447,
                                   "xs_mem_tmp_64447") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63545, "mem_63545") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63542, "mem_63542") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63539, "mem_63539") != 0)
                    return 1;
            }
            res_mem_sizze_63558 = xs_mem_sizze_63531;
            if (memblock_set(ctx, &res_mem_63559, &xs_mem_63532,
                             "xs_mem_63532") != 0)
                return 1;
            res_mem_sizze_63560 = xs_mem_sizze_63533;
            if (memblock_set(ctx, &res_mem_63561, &xs_mem_63534,
                             "xs_mem_63534") != 0)
                return 1;
            res_mem_sizze_63562 = xs_mem_sizze_63535;
            if (memblock_set(ctx, &res_mem_63563, &xs_mem_63536,
                             "xs_mem_63536") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64437 = res_mem_sizze_63558;
            struct memblock xs_mem_tmp_64438;
            
            xs_mem_tmp_64438.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64438, &res_mem_63559,
                             "res_mem_63559") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64439 = res_mem_sizze_63560;
            struct memblock xs_mem_tmp_64440;
            
            xs_mem_tmp_64440.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64440, &res_mem_63561,
                             "res_mem_63561") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64441 = res_mem_sizze_63562;
            struct memblock xs_mem_tmp_64442;
            
            xs_mem_tmp_64442.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64442, &res_mem_63563,
                             "res_mem_63563") != 0)
                return 1;
            xs_mem_sizze_63525 = xs_mem_sizze_tmp_64437;
            if (memblock_set(ctx, &xs_mem_63526, &xs_mem_tmp_64438,
                             "xs_mem_tmp_64438") != 0)
                return 1;
            xs_mem_sizze_63527 = xs_mem_sizze_tmp_64439;
            if (memblock_set(ctx, &xs_mem_63528, &xs_mem_tmp_64440,
                             "xs_mem_tmp_64440") != 0)
                return 1;
            xs_mem_sizze_63529 = xs_mem_sizze_tmp_64441;
            if (memblock_set(ctx, &xs_mem_63530, &xs_mem_tmp_64442,
                             "xs_mem_tmp_64442") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64442, "xs_mem_tmp_64442") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64440, "xs_mem_tmp_64440") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64438, "xs_mem_tmp_64438") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63536, "xs_mem_63536") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63534, "xs_mem_63534") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63532, "xs_mem_63532") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63563, "res_mem_63563") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63561, "res_mem_63561") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63559, "res_mem_63559") != 0)
                return 1;
        }
        indexed_mem_sizze_63564 = xs_mem_sizze_63525;
        if (memblock_set(ctx, &indexed_mem_63565, &xs_mem_63526,
                         "xs_mem_63526") != 0)
            return 1;
        indexed_mem_sizze_63566 = xs_mem_sizze_63527;
        if (memblock_set(ctx, &indexed_mem_63567, &xs_mem_63528,
                         "xs_mem_63528") != 0)
            return 1;
        indexed_mem_sizze_63568 = xs_mem_sizze_63529;
        if (memblock_set(ctx, &indexed_mem_63569, &xs_mem_63530,
                         "xs_mem_63530") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
            return 1;
        
        int32_t m_59385 = conc_tmp_59182 - 1;
        bool zzero_leq_i_p_m_t_s_59386 = sle32(0, m_59385);
        bool i_p_m_t_s_leq_w_59387 = slt32(m_59385, sizze_59188);
        bool y_59389 = zzero_leq_i_p_m_t_s_59386 && i_p_m_t_s_leq_w_59387;
        bool ok_or_empty_59391 = cond_59186 || y_59389;
        bool index_certs_59392;
        
        if (!ok_or_empty_59391) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                                   "Index [", "", ":", conc_tmp_59182,
                                   "] out of bounds for array of shape [",
                                   sizze_59188, "].");
            if (memblock_unref(ctx, &xs_mem_63530, "xs_mem_63530") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63528, "xs_mem_63528") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63526, "xs_mem_63526") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63569, "indexed_mem_63569") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63567, "indexed_mem_63567") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63565, "indexed_mem_63565") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        struct memblock mem_63572;
        
        mem_63572.references = NULL;
        if (memblock_alloc(ctx, &mem_63572, bytes_63468, "mem_63572"))
            return 1;
        
        struct memblock mem_63574;
        
        mem_63574.references = NULL;
        if (memblock_alloc(ctx, &mem_63574, binop_x_63469, "mem_63574"))
            return 1;
        
        int32_t discard_61385;
        int32_t scanacc_61370 = 1;
        
        for (int32_t i_61376 = 0; i_61376 < conc_tmp_59182; i_61376++) {
            int32_t x_59414 = *(int32_t *) &indexed_mem_63565.mem[i_61376 * 4];
            int32_t x_59415 = *(int32_t *) &indexed_mem_63567.mem[i_61376 * 4];
            int32_t i_p_o_62258 = -1 + i_61376;
            int32_t rot_i_62259 = smod32(i_p_o_62258, conc_tmp_59182);
            int32_t x_59416 = *(int32_t *) &indexed_mem_63565.mem[rot_i_62259 *
                                                                  4];
            int32_t x_59417 = *(int32_t *) &indexed_mem_63567.mem[rot_i_62259 *
                                                                  4];
            int32_t x_59418 = *(int32_t *) &indexed_mem_63569.mem[i_61376 * 4];
            bool res_59419 = x_59414 == x_59416;
            bool res_59420 = x_59415 == x_59417;
            bool eq_59421 = res_59419 && res_59420;
            bool res_59422 = !eq_59421;
            int32_t res_59412;
            
            if (res_59422) {
                res_59412 = x_59418;
            } else {
                int32_t res_59413 = x_59418 * scanacc_61370;
                
                res_59412 = res_59413;
            }
            *(int32_t *) &mem_63572.mem[i_61376 * 4] = res_59412;
            *(bool *) &mem_63574.mem[i_61376] = res_59422;
            
            int32_t scanacc_tmp_64458 = res_59412;
            
            scanacc_61370 = scanacc_tmp_64458;
        }
        discard_61385 = scanacc_61370;
        if (memblock_unref(ctx, &indexed_mem_63565, "indexed_mem_63565") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63567, "indexed_mem_63567") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63569, "indexed_mem_63569") != 0)
            return 1;
        
        struct memblock mem_63585;
        
        mem_63585.references = NULL;
        if (memblock_alloc(ctx, &mem_63585, bytes_63468, "mem_63585"))
            return 1;
        
        int32_t discard_61391;
        int32_t scanacc_61387 = 0;
        
        for (int32_t i_61389 = 0; i_61389 < conc_tmp_59182; i_61389++) {
            int32_t i_p_o_62266 = 1 + i_61389;
            int32_t rot_i_62267 = smod32(i_p_o_62266, conc_tmp_59182);
            bool x_59428 = *(bool *) &mem_63574.mem[rot_i_62267];
            int32_t res_59429 = btoi_bool_i32(x_59428);
            int32_t res_59427 = res_59429 + scanacc_61387;
            
            *(int32_t *) &mem_63585.mem[i_61389 * 4] = res_59427;
            
            int32_t scanacc_tmp_64461 = res_59427;
            
            scanacc_61387 = scanacc_tmp_64461;
        }
        discard_61391 = scanacc_61387;
        
        int32_t res_59430;
        
        if (loop_cond_59187) {
            bool index_certs_59433;
            
            if (!zzero_leq_i_p_m_t_s_59386) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                       "Index [", m_59385,
                                       "] out of bounds for array of shape [",
                                       conc_tmp_59182, "].");
                if (memblock_unref(ctx, &mem_63585, "mem_63585") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63574, "mem_63574") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63572, "mem_63572") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63530, "xs_mem_63530") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63528, "xs_mem_63528") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63526, "xs_mem_63526") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63569,
                                   "indexed_mem_63569") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63567,
                                   "indexed_mem_63567") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63565,
                                   "indexed_mem_63565") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63167,
                                   "indexed_mem_63167") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63165,
                                   "indexed_mem_63165") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63163,
                                   "indexed_mem_63163") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int32_t res_59434 = *(int32_t *) &mem_63585.mem[m_59385 * 4];
            
            res_59430 = res_59434;
        } else {
            res_59430 = 0;
        }
        
        bool bounds_invalid_upwards_59435 = slt32(res_59430, 0);
        bool eq_x_zz_59436 = 0 == res_59430;
        bool not_p_59437 = !bounds_invalid_upwards_59435;
        bool p_and_eq_x_y_59438 = eq_x_zz_59436 && not_p_59437;
        bool dim_zzero_59439 = bounds_invalid_upwards_59435 ||
             p_and_eq_x_y_59438;
        bool both_empty_59440 = eq_x_zz_59436 && dim_zzero_59439;
        bool eq_x_y_59441 = res_59430 == 0;
        bool empty_or_match_59444 = not_p_59437 || both_empty_59440;
        bool empty_or_match_cert_59445;
        
        if (!empty_or_match_59444) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:34-39 -> tupleTest.fut:149:13-43 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                                   "Function return value does not match shape of type ",
                                   "*", "[", res_59430, "]", "t");
            if (memblock_unref(ctx, &mem_63585, "mem_63585") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63574, "mem_63574") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63572, "mem_63572") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63530, "xs_mem_63530") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63528, "xs_mem_63528") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63526, "xs_mem_63526") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63569, "indexed_mem_63569") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63567, "indexed_mem_63567") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63565, "indexed_mem_63565") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_63591 = sext_i32_i64(res_59430);
        int64_t bytes_63590 = 4 * binop_x_63591;
        struct memblock mem_63592;
        
        mem_63592.references = NULL;
        if (memblock_alloc(ctx, &mem_63592, bytes_63590, "mem_63592"))
            return 1;
        for (int32_t i_64463 = 0; i_64463 < res_59430; i_64463++) {
            *(int32_t *) &mem_63592.mem[i_64463 * 4] = 1;
        }
        for (int32_t write_iter_61392 = 0; write_iter_61392 < conc_tmp_59182;
             write_iter_61392++) {
            int32_t write_iv_61396 =
                    *(int32_t *) &mem_63585.mem[write_iter_61392 * 4];
            int32_t i_p_o_62271 = 1 + write_iter_61392;
            int32_t rot_i_62272 = smod32(i_p_o_62271, conc_tmp_59182);
            bool write_iv_61397 = *(bool *) &mem_63574.mem[rot_i_62272];
            int32_t res_59457;
            
            if (write_iv_61397) {
                int32_t res_59458 = write_iv_61396 - 1;
                
                res_59457 = res_59458;
            } else {
                res_59457 = -1;
            }
            
            bool less_than_zzero_61413 = slt32(res_59457, 0);
            bool greater_than_sizze_61414 = sle32(res_59430, res_59457);
            bool outside_bounds_dim_61415 = less_than_zzero_61413 ||
                 greater_than_sizze_61414;
            
            if (!outside_bounds_dim_61415) {
                memmove(mem_63592.mem + res_59457 * 4, mem_63572.mem +
                        write_iter_61392 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63572, "mem_63572") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63574, "mem_63574") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63585, "mem_63585") != 0)
            return 1;
        
        struct memblock mem_63599;
        
        mem_63599.references = NULL;
        if (memblock_alloc(ctx, &mem_63599, bytes_63590, "mem_63599"))
            return 1;
        
        struct memblock mem_63602;
        
        mem_63602.references = NULL;
        if (memblock_alloc(ctx, &mem_63602, bytes_63590, "mem_63602"))
            return 1;
        
        int32_t discard_61427;
        int32_t scanacc_61421 = 0;
        
        for (int32_t i_61424 = 0; i_61424 < res_59430; i_61424++) {
            int32_t x_59464 = *(int32_t *) &mem_63592.mem[i_61424 * 4];
            bool not_arg_59465 = x_59464 == 0;
            bool res_59466 = !not_arg_59465;
            int32_t part_res_59467;
            
            if (res_59466) {
                part_res_59467 = 0;
            } else {
                part_res_59467 = 1;
            }
            
            int32_t part_res_59468;
            
            if (res_59466) {
                part_res_59468 = 1;
            } else {
                part_res_59468 = 0;
            }
            
            int32_t zz_59463 = part_res_59468 + scanacc_61421;
            
            *(int32_t *) &mem_63599.mem[i_61424 * 4] = zz_59463;
            *(int32_t *) &mem_63602.mem[i_61424 * 4] = part_res_59467;
            
            int32_t scanacc_tmp_64465 = zz_59463;
            
            scanacc_61421 = scanacc_tmp_64465;
        }
        discard_61427 = scanacc_61421;
        
        int32_t last_index_59469 = res_59430 - 1;
        int32_t partition_sizze_59470;
        
        if (eq_x_y_59441) {
            partition_sizze_59470 = 0;
        } else {
            int32_t last_offset_59471 =
                    *(int32_t *) &mem_63599.mem[last_index_59469 * 4];
            
            partition_sizze_59470 = last_offset_59471;
        }
        
        int64_t binop_x_63612 = sext_i32_i64(partition_sizze_59470);
        int64_t bytes_63611 = 4 * binop_x_63612;
        struct memblock mem_63613;
        
        mem_63613.references = NULL;
        if (memblock_alloc(ctx, &mem_63613, bytes_63611, "mem_63613"))
            return 1;
        for (int32_t write_iter_61428 = 0; write_iter_61428 < res_59430;
             write_iter_61428++) {
            int32_t write_iv_61430 =
                    *(int32_t *) &mem_63602.mem[write_iter_61428 * 4];
            int32_t write_iv_61431 =
                    *(int32_t *) &mem_63599.mem[write_iter_61428 * 4];
            bool is_this_one_59479 = write_iv_61430 == 0;
            int32_t this_offset_59480 = -1 + write_iv_61431;
            int32_t total_res_59481;
            
            if (is_this_one_59479) {
                total_res_59481 = this_offset_59480;
            } else {
                total_res_59481 = -1;
            }
            
            bool less_than_zzero_61435 = slt32(total_res_59481, 0);
            bool greater_than_sizze_61436 = sle32(partition_sizze_59470,
                                                  total_res_59481);
            bool outside_bounds_dim_61437 = less_than_zzero_61435 ||
                 greater_than_sizze_61436;
            
            if (!outside_bounds_dim_61437) {
                memmove(mem_63613.mem + total_res_59481 * 4, mem_63592.mem +
                        write_iter_61428 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63592, "mem_63592") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63599, "mem_63599") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63602, "mem_63602") != 0)
            return 1;
        
        bool res_59492;
        bool redout_61441 = 1;
        
        for (int32_t i_61442 = 0; i_61442 < 5; i_61442++) {
            int32_t x_59497 = 1 + i_61442;
            int32_t x_59498 = pow32(x_59497, 2);
            int32_t y_59499 = *(int32_t *) &mem_63613.mem[i_61442 * 4];
            bool res_59500 = x_59498 == y_59499;
            bool x_59495 = res_59500 && redout_61441;
            bool redout_tmp_64469 = x_59495;
            
            redout_61441 = redout_tmp_64469;
        }
        res_59492 = redout_61441;
        if (memblock_unref(ctx, &mem_63613, "mem_63613") != 0)
            return 1;
        cond_59143 = res_59492;
        if (memblock_unref(ctx, &mem_63613, "mem_63613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63602, "mem_63602") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63599, "mem_63599") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63592, "mem_63592") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63585, "mem_63585") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63574, "mem_63574") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63572, "mem_63572") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63530, "xs_mem_63530") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63528, "xs_mem_63528") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63526, "xs_mem_63526") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63569, "indexed_mem_63569") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63567, "indexed_mem_63567") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63565, "indexed_mem_63565") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63524, "res_mem_63524") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63522, "res_mem_63522") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63520, "res_mem_63520") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63476, "mem_63476") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63473, "mem_63473") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63470, "mem_63470") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63449, "mem_63449") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63446, "mem_63446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63443, "mem_63443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63432, "mem_63432") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63429, "mem_63429") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63424, "mem_63424") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63419, "mem_63419") != 0)
            return 1;
    } else {
        cond_59143 = 0;
    }
    
    bool res_59501;
    
    if (cond_59143) {
        struct memblock mem_63620;
        
        mem_63620.references = NULL;
        if (memblock_alloc(ctx, &mem_63620, 16, "mem_63620"))
            return 1;
        
        struct memblock mem_63625;
        
        mem_63625.references = NULL;
        if (memblock_alloc(ctx, &mem_63625, 8, "mem_63625"))
            return 1;
        for (int32_t i_61445 = 0; i_61445 < 2; i_61445++) {
            for (int32_t i_64471 = 0; i_64471 < 2; i_64471++) {
                *(int32_t *) &mem_63625.mem[i_64471 * 4] = i_61445;
            }
            memmove(mem_63620.mem + 2 * i_61445 * 4, mem_63625.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
            return 1;
        
        struct memblock mem_63630;
        
        mem_63630.references = NULL;
        if (memblock_alloc(ctx, &mem_63630, 16, "mem_63630"))
            return 1;
        
        struct memblock mem_63633;
        
        mem_63633.references = NULL;
        if (memblock_alloc(ctx, &mem_63633, 16, "mem_63633"))
            return 1;
        
        struct memblock mem_63636;
        
        mem_63636.references = NULL;
        if (memblock_alloc(ctx, &mem_63636, 16, "mem_63636"))
            return 1;
        
        struct memblock mem_63639;
        
        mem_63639.references = NULL;
        if (memblock_alloc(ctx, &mem_63639, 16, "mem_63639"))
            return 1;
        
        int32_t discard_61461;
        int32_t scanacc_61451 = 0;
        
        for (int32_t i_61456 = 0; i_61456 < 4; i_61456++) {
            int32_t x_59514 = smod32(i_61456, 2);
            bool cond_59515 = x_59514 == 0;
            int32_t res_59516;
            
            if (cond_59515) {
                res_59516 = 0;
            } else {
                res_59516 = 1;
            }
            
            bool cond_59517 = x_59514 == 1;
            int32_t res_59518;
            
            if (cond_59517) {
                res_59518 = 0;
            } else {
                res_59518 = 1;
            }
            
            bool res_59519 = !cond_59515;
            int32_t part_res_59520;
            
            if (res_59519) {
                part_res_59520 = 0;
            } else {
                part_res_59520 = 1;
            }
            
            int32_t part_res_59521;
            
            if (res_59519) {
                part_res_59521 = 1;
            } else {
                part_res_59521 = 0;
            }
            
            int32_t zz_59512 = part_res_59521 + scanacc_61451;
            
            *(int32_t *) &mem_63630.mem[i_61456 * 4] = zz_59512;
            *(int32_t *) &mem_63633.mem[i_61456 * 4] = part_res_59520;
            *(int32_t *) &mem_63636.mem[i_61456 * 4] = res_59518;
            *(int32_t *) &mem_63639.mem[i_61456 * 4] = res_59516;
            
            int32_t scanacc_tmp_64472 = zz_59512;
            
            scanacc_61451 = scanacc_tmp_64472;
        }
        discard_61461 = scanacc_61451;
        
        int32_t last_offset_59522 = *(int32_t *) &mem_63630.mem[12];
        int64_t binop_x_63657 = sext_i32_i64(last_offset_59522);
        int64_t bytes_63656 = 4 * binop_x_63657;
        struct memblock mem_63658;
        
        mem_63658.references = NULL;
        if (memblock_alloc(ctx, &mem_63658, bytes_63656, "mem_63658"))
            return 1;
        
        struct memblock mem_63661;
        
        mem_63661.references = NULL;
        if (memblock_alloc(ctx, &mem_63661, bytes_63656, "mem_63661"))
            return 1;
        
        struct memblock mem_63664;
        
        mem_63664.references = NULL;
        if (memblock_alloc(ctx, &mem_63664, bytes_63656, "mem_63664"))
            return 1;
        for (int32_t write_iter_61462 = 0; write_iter_61462 < 4;
             write_iter_61462++) {
            int32_t write_iv_61466 =
                    *(int32_t *) &mem_63633.mem[write_iter_61462 * 4];
            int32_t write_iv_61467 =
                    *(int32_t *) &mem_63630.mem[write_iter_61462 * 4];
            int32_t new_index_62277 = squot32(write_iter_61462, 2);
            int32_t binop_y_62279 = 2 * new_index_62277;
            int32_t new_index_62280 = write_iter_61462 - binop_y_62279;
            bool is_this_one_59534 = write_iv_61466 == 0;
            int32_t this_offset_59535 = -1 + write_iv_61467;
            int32_t total_res_59536;
            
            if (is_this_one_59534) {
                total_res_59536 = this_offset_59535;
            } else {
                total_res_59536 = -1;
            }
            
            bool less_than_zzero_61471 = slt32(total_res_59536, 0);
            bool greater_than_sizze_61472 = sle32(last_offset_59522,
                                                  total_res_59536);
            bool outside_bounds_dim_61473 = less_than_zzero_61471 ||
                 greater_than_sizze_61472;
            
            if (!outside_bounds_dim_61473) {
                memmove(mem_63658.mem + total_res_59536 * 4, mem_63620.mem +
                        (2 * new_index_62277 + new_index_62280) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_61473) {
                struct memblock mem_63673;
                
                mem_63673.references = NULL;
                if (memblock_alloc(ctx, &mem_63673, 4, "mem_63673"))
                    return 1;
                
                int32_t x_64481;
                
                for (int32_t i_64480 = 0; i_64480 < 1; i_64480++) {
                    x_64481 = new_index_62280 + sext_i32_i32(i_64480);
                    *(int32_t *) &mem_63673.mem[i_64480 * 4] = x_64481;
                }
                memmove(mem_63661.mem + total_res_59536 * 4, mem_63673.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63673, "mem_63673") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63673, "mem_63673") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_61473) {
                memmove(mem_63664.mem + total_res_59536 * 4, mem_63639.mem +
                        write_iter_61462 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
            return 1;
        
        struct memblock mem_63682;
        
        mem_63682.references = NULL;
        if (memblock_alloc(ctx, &mem_63682, 16, "mem_63682"))
            return 1;
        
        struct memblock mem_63687;
        
        mem_63687.references = NULL;
        if (memblock_alloc(ctx, &mem_63687, 8, "mem_63687"))
            return 1;
        for (int32_t i_61491 = 0; i_61491 < 2; i_61491++) {
            for (int32_t i_64483 = 0; i_64483 < 2; i_64483++) {
                *(int32_t *) &mem_63687.mem[i_64483 * 4] = i_61491;
            }
            memmove(mem_63682.mem + 2 * i_61491 * 4, mem_63687.mem + 0, 2 *
                    sizeof(int32_t));
        }
        if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
            return 1;
        
        struct memblock mem_63692;
        
        mem_63692.references = NULL;
        if (memblock_alloc(ctx, &mem_63692, 16, "mem_63692"))
            return 1;
        
        struct memblock mem_63695;
        
        mem_63695.references = NULL;
        if (memblock_alloc(ctx, &mem_63695, 16, "mem_63695"))
            return 1;
        
        int32_t discard_61501;
        int32_t scanacc_61495 = 0;
        
        for (int32_t i_61498 = 0; i_61498 < 4; i_61498++) {
            int32_t x_59555 = *(int32_t *) &mem_63636.mem[i_61498 * 4];
            bool not_arg_59556 = x_59555 == 0;
            bool res_59557 = !not_arg_59556;
            int32_t part_res_59558;
            
            if (res_59557) {
                part_res_59558 = 0;
            } else {
                part_res_59558 = 1;
            }
            
            int32_t part_res_59559;
            
            if (res_59557) {
                part_res_59559 = 1;
            } else {
                part_res_59559 = 0;
            }
            
            int32_t zz_59554 = part_res_59559 + scanacc_61495;
            
            *(int32_t *) &mem_63692.mem[i_61498 * 4] = zz_59554;
            *(int32_t *) &mem_63695.mem[i_61498 * 4] = part_res_59558;
            
            int32_t scanacc_tmp_64484 = zz_59554;
            
            scanacc_61495 = scanacc_tmp_64484;
        }
        discard_61501 = scanacc_61495;
        
        int32_t last_offset_59560 = *(int32_t *) &mem_63692.mem[12];
        int64_t binop_x_63705 = sext_i32_i64(last_offset_59560);
        int64_t bytes_63704 = 4 * binop_x_63705;
        struct memblock mem_63706;
        
        mem_63706.references = NULL;
        if (memblock_alloc(ctx, &mem_63706, bytes_63704, "mem_63706"))
            return 1;
        
        struct memblock mem_63709;
        
        mem_63709.references = NULL;
        if (memblock_alloc(ctx, &mem_63709, bytes_63704, "mem_63709"))
            return 1;
        
        struct memblock mem_63712;
        
        mem_63712.references = NULL;
        if (memblock_alloc(ctx, &mem_63712, bytes_63704, "mem_63712"))
            return 1;
        for (int32_t write_iter_61502 = 0; write_iter_61502 < 4;
             write_iter_61502++) {
            int32_t write_iv_61506 =
                    *(int32_t *) &mem_63695.mem[write_iter_61502 * 4];
            int32_t write_iv_61507 =
                    *(int32_t *) &mem_63692.mem[write_iter_61502 * 4];
            int32_t new_index_62292 = squot32(write_iter_61502, 2);
            int32_t binop_y_62294 = 2 * new_index_62292;
            int32_t new_index_62295 = write_iter_61502 - binop_y_62294;
            bool is_this_one_59572 = write_iv_61506 == 0;
            int32_t this_offset_59573 = -1 + write_iv_61507;
            int32_t total_res_59574;
            
            if (is_this_one_59572) {
                total_res_59574 = this_offset_59573;
            } else {
                total_res_59574 = -1;
            }
            
            bool less_than_zzero_61511 = slt32(total_res_59574, 0);
            bool greater_than_sizze_61512 = sle32(last_offset_59560,
                                                  total_res_59574);
            bool outside_bounds_dim_61513 = less_than_zzero_61511 ||
                 greater_than_sizze_61512;
            
            if (!outside_bounds_dim_61513) {
                memmove(mem_63706.mem + total_res_59574 * 4, mem_63682.mem +
                        (2 * new_index_62292 + new_index_62295) * 4,
                        sizeof(int32_t));
            }
            if (!outside_bounds_dim_61513) {
                struct memblock mem_63721;
                
                mem_63721.references = NULL;
                if (memblock_alloc(ctx, &mem_63721, 4, "mem_63721"))
                    return 1;
                
                int32_t x_64491;
                
                for (int32_t i_64490 = 0; i_64490 < 1; i_64490++) {
                    x_64491 = new_index_62295 + sext_i32_i32(i_64490);
                    *(int32_t *) &mem_63721.mem[i_64490 * 4] = x_64491;
                }
                memmove(mem_63709.mem + total_res_59574 * 4, mem_63721.mem + 0,
                        sizeof(int32_t));
                if (memblock_unref(ctx, &mem_63721, "mem_63721") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63721, "mem_63721") != 0)
                    return 1;
            }
            if (!outside_bounds_dim_61513) {
                memmove(mem_63712.mem + total_res_59574 * 4, mem_63636.mem +
                        write_iter_61502 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
            return 1;
        
        int32_t conc_tmp_59584 = last_offset_59522 + last_offset_59560;
        int64_t binop_x_63729 = sext_i32_i64(conc_tmp_59584);
        int64_t bytes_63728 = 4 * binop_x_63729;
        struct memblock mem_63730;
        
        mem_63730.references = NULL;
        if (memblock_alloc(ctx, &mem_63730, bytes_63728, "mem_63730"))
            return 1;
        
        int32_t tmp_offs_64492 = 0;
        
        memmove(mem_63730.mem + tmp_offs_64492 * 4, mem_63658.mem + 0,
                last_offset_59522 * sizeof(int32_t));
        tmp_offs_64492 += last_offset_59522;
        memmove(mem_63730.mem + tmp_offs_64492 * 4, mem_63706.mem + 0,
                last_offset_59560 * sizeof(int32_t));
        tmp_offs_64492 += last_offset_59560;
        
        struct memblock mem_63733;
        
        mem_63733.references = NULL;
        if (memblock_alloc(ctx, &mem_63733, bytes_63728, "mem_63733"))
            return 1;
        
        int32_t tmp_offs_64493 = 0;
        
        memmove(mem_63733.mem + tmp_offs_64493 * 4, mem_63661.mem + 0,
                last_offset_59522 * sizeof(int32_t));
        tmp_offs_64493 += last_offset_59522;
        memmove(mem_63733.mem + tmp_offs_64493 * 4, mem_63709.mem + 0,
                last_offset_59560 * sizeof(int32_t));
        tmp_offs_64493 += last_offset_59560;
        
        struct memblock mem_63736;
        
        mem_63736.references = NULL;
        if (memblock_alloc(ctx, &mem_63736, bytes_63728, "mem_63736"))
            return 1;
        
        int32_t tmp_offs_64494 = 0;
        
        memmove(mem_63736.mem + tmp_offs_64494 * 4, mem_63664.mem + 0,
                last_offset_59522 * sizeof(int32_t));
        tmp_offs_64494 += last_offset_59522;
        memmove(mem_63736.mem + tmp_offs_64494 * 4, mem_63712.mem + 0,
                last_offset_59560 * sizeof(int32_t));
        tmp_offs_64494 += last_offset_59560;
        
        bool cond_59588 = conc_tmp_59584 == 0;
        bool loop_cond_59589 = slt32(1, conc_tmp_59584);
        int32_t sizze_59590;
        int32_t sizze_59591;
        int32_t sizze_59592;
        int64_t res_mem_sizze_63779;
        struct memblock res_mem_63780;
        
        res_mem_63780.references = NULL;
        
        int64_t res_mem_sizze_63781;
        struct memblock res_mem_63782;
        
        res_mem_63782.references = NULL;
        
        int64_t res_mem_sizze_63783;
        struct memblock res_mem_63784;
        
        res_mem_63784.references = NULL;
        
        int32_t res_59596;
        
        if (cond_59588) {
            struct memblock mem_63739;
            
            mem_63739.references = NULL;
            if (memblock_alloc(ctx, &mem_63739, bytes_63728, "mem_63739"))
                return 1;
            memmove(mem_63739.mem + 0, mem_63730.mem + 0, conc_tmp_59584 *
                    sizeof(int32_t));
            
            struct memblock mem_63742;
            
            mem_63742.references = NULL;
            if (memblock_alloc(ctx, &mem_63742, bytes_63728, "mem_63742"))
                return 1;
            memmove(mem_63742.mem + 0, mem_63733.mem + 0, conc_tmp_59584 *
                    sizeof(int32_t));
            
            struct memblock mem_63745;
            
            mem_63745.references = NULL;
            if (memblock_alloc(ctx, &mem_63745, bytes_63728, "mem_63745"))
                return 1;
            memmove(mem_63745.mem + 0, mem_63736.mem + 0, conc_tmp_59584 *
                    sizeof(int32_t));
            sizze_59590 = conc_tmp_59584;
            sizze_59591 = conc_tmp_59584;
            sizze_59592 = conc_tmp_59584;
            res_mem_sizze_63779 = bytes_63728;
            if (memblock_set(ctx, &res_mem_63780, &mem_63739, "mem_63739") != 0)
                return 1;
            res_mem_sizze_63781 = bytes_63728;
            if (memblock_set(ctx, &res_mem_63782, &mem_63742, "mem_63742") != 0)
                return 1;
            res_mem_sizze_63783 = bytes_63728;
            if (memblock_set(ctx, &res_mem_63784, &mem_63745, "mem_63745") != 0)
                return 1;
            res_59596 = 0;
            if (memblock_unref(ctx, &mem_63745, "mem_63745") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63742, "mem_63742") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63739, "mem_63739") != 0)
                return 1;
        } else {
            bool res_59608;
            int32_t res_59609;
            int32_t res_59610;
            bool loop_while_59611;
            int32_t r_59612;
            int32_t n_59613;
            
            loop_while_59611 = loop_cond_59589;
            r_59612 = 0;
            n_59613 = conc_tmp_59584;
            while (loop_while_59611) {
                int32_t res_59614 = sdiv32(n_59613, 2);
                int32_t res_59615 = 1 + r_59612;
                bool loop_cond_59616 = slt32(1, res_59614);
                bool loop_while_tmp_64495 = loop_cond_59616;
                int32_t r_tmp_64496 = res_59615;
                int32_t n_tmp_64497;
                
                n_tmp_64497 = res_59614;
                loop_while_59611 = loop_while_tmp_64495;
                r_59612 = r_tmp_64496;
                n_59613 = n_tmp_64497;
            }
            res_59608 = loop_while_59611;
            res_59609 = r_59612;
            res_59610 = n_59613;
            
            int32_t y_59617 = 1 << res_59609;
            bool cond_59618 = conc_tmp_59584 == y_59617;
            int32_t y_59619 = 1 + res_59609;
            int32_t x_59620 = 1 << y_59619;
            int32_t arg_59621 = x_59620 - conc_tmp_59584;
            bool bounds_invalid_upwards_59622 = slt32(arg_59621, 0);
            int32_t conc_tmp_59623 = conc_tmp_59584 + arg_59621;
            int32_t sizze_59624;
            
            if (cond_59618) {
                sizze_59624 = conc_tmp_59584;
            } else {
                sizze_59624 = conc_tmp_59623;
            }
            
            int32_t res_59625;
            
            if (cond_59618) {
                res_59625 = res_59609;
            } else {
                res_59625 = y_59619;
            }
            
            int64_t binop_x_63765 = sext_i32_i64(conc_tmp_59623);
            int64_t bytes_63764 = 4 * binop_x_63765;
            int64_t res_mem_sizze_63773;
            struct memblock res_mem_63774;
            
            res_mem_63774.references = NULL;
            
            int64_t res_mem_sizze_63775;
            struct memblock res_mem_63776;
            
            res_mem_63776.references = NULL;
            
            int64_t res_mem_sizze_63777;
            struct memblock res_mem_63778;
            
            res_mem_63778.references = NULL;
            if (cond_59618) {
                struct memblock mem_63748;
                
                mem_63748.references = NULL;
                if (memblock_alloc(ctx, &mem_63748, bytes_63728, "mem_63748"))
                    return 1;
                memmove(mem_63748.mem + 0, mem_63730.mem + 0, conc_tmp_59584 *
                        sizeof(int32_t));
                
                struct memblock mem_63751;
                
                mem_63751.references = NULL;
                if (memblock_alloc(ctx, &mem_63751, bytes_63728, "mem_63751"))
                    return 1;
                memmove(mem_63751.mem + 0, mem_63733.mem + 0, conc_tmp_59584 *
                        sizeof(int32_t));
                
                struct memblock mem_63754;
                
                mem_63754.references = NULL;
                if (memblock_alloc(ctx, &mem_63754, bytes_63728, "mem_63754"))
                    return 1;
                memmove(mem_63754.mem + 0, mem_63736.mem + 0, conc_tmp_59584 *
                        sizeof(int32_t));
                res_mem_sizze_63773 = bytes_63728;
                if (memblock_set(ctx, &res_mem_63774, &mem_63748,
                                 "mem_63748") != 0)
                    return 1;
                res_mem_sizze_63775 = bytes_63728;
                if (memblock_set(ctx, &res_mem_63776, &mem_63751,
                                 "mem_63751") != 0)
                    return 1;
                res_mem_sizze_63777 = bytes_63728;
                if (memblock_set(ctx, &res_mem_63778, &mem_63754,
                                 "mem_63754") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63754, "mem_63754") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63751, "mem_63751") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63748, "mem_63748") != 0)
                    return 1;
            } else {
                bool y_59643 = slt32(0, conc_tmp_59584);
                bool index_certs_59644;
                
                if (!y_59643) {
                    ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                           "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                           "Index [", 0,
                                           "] out of bounds for array of shape [",
                                           conc_tmp_59584, "].");
                    if (memblock_unref(ctx, &res_mem_63778, "res_mem_63778") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63776, "res_mem_63776") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63774, "res_mem_63774") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63167,
                                       "indexed_mem_63167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63165,
                                       "indexed_mem_63165") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63163,
                                       "indexed_mem_63163") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                        return 1;
                    return 1;
                }
                
                bool index_concat_cmp_59645 = sle32(last_offset_59522, 0);
                int32_t index_concat_branch_59646;
                
                if (index_concat_cmp_59645) {
                    int32_t index_concat_i_59647 = 0 - last_offset_59522;
                    int32_t index_concat_59648 =
                            *(int32_t *) &mem_63706.mem[index_concat_i_59647 *
                                                        4];
                    
                    index_concat_branch_59646 = index_concat_59648;
                } else {
                    int32_t index_concat_59649 = *(int32_t *) &mem_63658.mem[0];
                    
                    index_concat_branch_59646 = index_concat_59649;
                }
                
                int32_t index_concat_branch_59650;
                
                if (index_concat_cmp_59645) {
                    int32_t index_concat_i_59651 = 0 - last_offset_59522;
                    int32_t index_concat_59652 =
                            *(int32_t *) &mem_63709.mem[index_concat_i_59651 *
                                                        4];
                    
                    index_concat_branch_59650 = index_concat_59652;
                } else {
                    int32_t index_concat_59653 = *(int32_t *) &mem_63661.mem[0];
                    
                    index_concat_branch_59650 = index_concat_59653;
                }
                
                int32_t index_concat_branch_59654;
                
                if (index_concat_cmp_59645) {
                    int32_t index_concat_i_59655 = 0 - last_offset_59522;
                    int32_t index_concat_59656 =
                            *(int32_t *) &mem_63712.mem[index_concat_i_59655 *
                                                        4];
                    
                    index_concat_branch_59654 = index_concat_59656;
                } else {
                    int32_t index_concat_59657 = *(int32_t *) &mem_63664.mem[0];
                    
                    index_concat_branch_59654 = index_concat_59657;
                }
                
                int32_t res_59658;
                int32_t res_59659;
                int32_t res_59660;
                int32_t redout_61529;
                int32_t redout_61530;
                int32_t redout_61531;
                
                redout_61529 = index_concat_branch_59646;
                redout_61530 = index_concat_branch_59650;
                redout_61531 = index_concat_branch_59654;
                for (int32_t i_61532 = 0; i_61532 < conc_tmp_59584; i_61532++) {
                    bool index_concat_cmp_62319 = sle32(last_offset_59522,
                                                        i_61532);
                    int32_t index_concat_branch_62323;
                    
                    if (index_concat_cmp_62319) {
                        int32_t index_concat_i_62320 = i_61532 -
                                last_offset_59522;
                        int32_t index_concat_62321 =
                                *(int32_t *) &mem_63706.mem[index_concat_i_62320 *
                                                            4];
                        
                        index_concat_branch_62323 = index_concat_62321;
                    } else {
                        int32_t index_concat_62322 =
                                *(int32_t *) &mem_63658.mem[i_61532 * 4];
                        
                        index_concat_branch_62323 = index_concat_62322;
                    }
                    
                    int32_t index_concat_branch_62317;
                    
                    if (index_concat_cmp_62319) {
                        int32_t index_concat_i_62314 = i_61532 -
                                last_offset_59522;
                        int32_t index_concat_62315 =
                                *(int32_t *) &mem_63709.mem[index_concat_i_62314 *
                                                            4];
                        
                        index_concat_branch_62317 = index_concat_62315;
                    } else {
                        int32_t index_concat_62316 =
                                *(int32_t *) &mem_63661.mem[i_61532 * 4];
                        
                        index_concat_branch_62317 = index_concat_62316;
                    }
                    
                    int32_t index_concat_branch_62311;
                    
                    if (index_concat_cmp_62319) {
                        int32_t index_concat_i_62308 = i_61532 -
                                last_offset_59522;
                        int32_t index_concat_62309 =
                                *(int32_t *) &mem_63712.mem[index_concat_i_62308 *
                                                            4];
                        
                        index_concat_branch_62311 = index_concat_62309;
                    } else {
                        int32_t index_concat_62310 =
                                *(int32_t *) &mem_63664.mem[i_61532 * 4];
                        
                        index_concat_branch_62311 = index_concat_62310;
                    }
                    
                    bool cond_59667 = redout_61529 == index_concat_branch_62323;
                    bool res_59668 = sle32(redout_61530,
                                           index_concat_branch_62317);
                    bool res_59669 = sle32(redout_61529,
                                           index_concat_branch_62323);
                    bool x_59670 = cond_59667 && res_59668;
                    bool x_59671 = !cond_59667;
                    bool y_59672 = res_59669 && x_59671;
                    bool res_59673 = x_59670 || y_59672;
                    int32_t res_59674;
                    
                    if (res_59673) {
                        res_59674 = index_concat_branch_62323;
                    } else {
                        res_59674 = redout_61529;
                    }
                    
                    int32_t res_59675;
                    
                    if (res_59673) {
                        res_59675 = index_concat_branch_62317;
                    } else {
                        res_59675 = redout_61530;
                    }
                    
                    int32_t res_59676;
                    
                    if (res_59673) {
                        res_59676 = index_concat_branch_62311;
                    } else {
                        res_59676 = redout_61531;
                    }
                    
                    int32_t redout_tmp_64498 = res_59674;
                    int32_t redout_tmp_64499 = res_59675;
                    int32_t redout_tmp_64500;
                    
                    redout_tmp_64500 = res_59676;
                    redout_61529 = redout_tmp_64498;
                    redout_61530 = redout_tmp_64499;
                    redout_61531 = redout_tmp_64500;
                }
                res_59658 = redout_61529;
                res_59659 = redout_61530;
                res_59660 = redout_61531;
                
                bool eq_x_zz_59680 = 0 == arg_59621;
                bool not_p_59681 = !bounds_invalid_upwards_59622;
                bool p_and_eq_x_y_59682 = eq_x_zz_59680 && not_p_59681;
                bool dim_zzero_59683 = bounds_invalid_upwards_59622 ||
                     p_and_eq_x_y_59682;
                bool both_empty_59684 = eq_x_zz_59680 && dim_zzero_59683;
                bool eq_x_y_59685 = arg_59621 == 0;
                bool p_and_eq_x_y_59686 = bounds_invalid_upwards_59622 &&
                     eq_x_y_59685;
                bool dim_match_59687 = not_p_59681 || p_and_eq_x_y_59686;
                bool empty_or_match_59688 = both_empty_59684 || dim_match_59687;
                bool empty_or_match_cert_59689;
                
                if (!empty_or_match_59688) {
                    ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                           "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                           "Function return value does not match shape of type ",
                                           "*", "[", arg_59621, "]", "t");
                    if (memblock_unref(ctx, &res_mem_63778, "res_mem_63778") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63776, "res_mem_63776") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63774, "res_mem_63774") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                        return 1;
                    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63167,
                                       "indexed_mem_63167") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63165,
                                       "indexed_mem_63165") != 0)
                        return 1;
                    if (memblock_unref(ctx, &indexed_mem_63163,
                                       "indexed_mem_63163") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") !=
                        0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                        return 1;
                    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                        return 1;
                    return 1;
                }
                
                int64_t binop_x_63756 = sext_i32_i64(arg_59621);
                int64_t bytes_63755 = 4 * binop_x_63756;
                struct memblock mem_63757;
                
                mem_63757.references = NULL;
                if (memblock_alloc(ctx, &mem_63757, bytes_63755, "mem_63757"))
                    return 1;
                for (int32_t i_64501 = 0; i_64501 < arg_59621; i_64501++) {
                    *(int32_t *) &mem_63757.mem[i_64501 * 4] = res_59658;
                }
                
                struct memblock mem_63760;
                
                mem_63760.references = NULL;
                if (memblock_alloc(ctx, &mem_63760, bytes_63755, "mem_63760"))
                    return 1;
                for (int32_t i_64502 = 0; i_64502 < arg_59621; i_64502++) {
                    *(int32_t *) &mem_63760.mem[i_64502 * 4] = res_59659;
                }
                
                struct memblock mem_63763;
                
                mem_63763.references = NULL;
                if (memblock_alloc(ctx, &mem_63763, bytes_63755, "mem_63763"))
                    return 1;
                for (int32_t i_64503 = 0; i_64503 < arg_59621; i_64503++) {
                    *(int32_t *) &mem_63763.mem[i_64503 * 4] = res_59660;
                }
                
                struct memblock mem_63766;
                
                mem_63766.references = NULL;
                if (memblock_alloc(ctx, &mem_63766, bytes_63764, "mem_63766"))
                    return 1;
                
                int32_t tmp_offs_64504 = 0;
                
                memmove(mem_63766.mem + tmp_offs_64504 * 4, mem_63658.mem + 0,
                        last_offset_59522 * sizeof(int32_t));
                tmp_offs_64504 += last_offset_59522;
                memmove(mem_63766.mem + tmp_offs_64504 * 4, mem_63706.mem + 0,
                        last_offset_59560 * sizeof(int32_t));
                tmp_offs_64504 += last_offset_59560;
                memmove(mem_63766.mem + tmp_offs_64504 * 4, mem_63757.mem + 0,
                        arg_59621 * sizeof(int32_t));
                tmp_offs_64504 += arg_59621;
                if (memblock_unref(ctx, &mem_63757, "mem_63757") != 0)
                    return 1;
                
                struct memblock mem_63769;
                
                mem_63769.references = NULL;
                if (memblock_alloc(ctx, &mem_63769, bytes_63764, "mem_63769"))
                    return 1;
                
                int32_t tmp_offs_64505 = 0;
                
                memmove(mem_63769.mem + tmp_offs_64505 * 4, mem_63661.mem + 0,
                        last_offset_59522 * sizeof(int32_t));
                tmp_offs_64505 += last_offset_59522;
                memmove(mem_63769.mem + tmp_offs_64505 * 4, mem_63709.mem + 0,
                        last_offset_59560 * sizeof(int32_t));
                tmp_offs_64505 += last_offset_59560;
                memmove(mem_63769.mem + tmp_offs_64505 * 4, mem_63760.mem + 0,
                        arg_59621 * sizeof(int32_t));
                tmp_offs_64505 += arg_59621;
                if (memblock_unref(ctx, &mem_63760, "mem_63760") != 0)
                    return 1;
                
                struct memblock mem_63772;
                
                mem_63772.references = NULL;
                if (memblock_alloc(ctx, &mem_63772, bytes_63764, "mem_63772"))
                    return 1;
                
                int32_t tmp_offs_64506 = 0;
                
                memmove(mem_63772.mem + tmp_offs_64506 * 4, mem_63664.mem + 0,
                        last_offset_59522 * sizeof(int32_t));
                tmp_offs_64506 += last_offset_59522;
                memmove(mem_63772.mem + tmp_offs_64506 * 4, mem_63712.mem + 0,
                        last_offset_59560 * sizeof(int32_t));
                tmp_offs_64506 += last_offset_59560;
                memmove(mem_63772.mem + tmp_offs_64506 * 4, mem_63763.mem + 0,
                        arg_59621 * sizeof(int32_t));
                tmp_offs_64506 += arg_59621;
                if (memblock_unref(ctx, &mem_63763, "mem_63763") != 0)
                    return 1;
                res_mem_sizze_63773 = bytes_63764;
                if (memblock_set(ctx, &res_mem_63774, &mem_63766,
                                 "mem_63766") != 0)
                    return 1;
                res_mem_sizze_63775 = bytes_63764;
                if (memblock_set(ctx, &res_mem_63776, &mem_63769,
                                 "mem_63769") != 0)
                    return 1;
                res_mem_sizze_63777 = bytes_63764;
                if (memblock_set(ctx, &res_mem_63778, &mem_63772,
                                 "mem_63772") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63772, "mem_63772") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63769, "mem_63769") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63766, "mem_63766") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63763, "mem_63763") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63760, "mem_63760") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63757, "mem_63757") != 0)
                    return 1;
            }
            sizze_59590 = sizze_59624;
            sizze_59591 = sizze_59624;
            sizze_59592 = sizze_59624;
            res_mem_sizze_63779 = res_mem_sizze_63773;
            if (memblock_set(ctx, &res_mem_63780, &res_mem_63774,
                             "res_mem_63774") != 0)
                return 1;
            res_mem_sizze_63781 = res_mem_sizze_63775;
            if (memblock_set(ctx, &res_mem_63782, &res_mem_63776,
                             "res_mem_63776") != 0)
                return 1;
            res_mem_sizze_63783 = res_mem_sizze_63777;
            if (memblock_set(ctx, &res_mem_63784, &res_mem_63778,
                             "res_mem_63778") != 0)
                return 1;
            res_59596 = res_59625;
            if (memblock_unref(ctx, &res_mem_63778, "res_mem_63778") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63776, "res_mem_63776") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63774, "res_mem_63774") != 0)
                return 1;
        }
        if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
            return 1;
        
        bool dim_zzero_59699 = 0 == sizze_59591;
        bool dim_zzero_59700 = 0 == sizze_59590;
        bool both_empty_59701 = dim_zzero_59699 && dim_zzero_59700;
        bool dim_match_59702 = sizze_59590 == sizze_59591;
        bool empty_or_match_59703 = both_empty_59701 || dim_match_59702;
        bool empty_or_match_cert_59704;
        
        if (!empty_or_match_59703) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        bool dim_zzero_59706 = 0 == sizze_59592;
        bool both_empty_59707 = dim_zzero_59700 && dim_zzero_59706;
        bool dim_match_59708 = sizze_59590 == sizze_59592;
        bool empty_or_match_59709 = both_empty_59707 || dim_match_59708;
        bool empty_or_match_cert_59710;
        
        if (!empty_or_match_59709) {
            ctx->error = msgprintf("Error at %s:\n%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                                   "Function return value does not match shape of declared return type.");
            if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_63798 = sext_i32_i64(sizze_59590);
        int64_t bytes_63797 = 4 * binop_x_63798;
        int64_t indexed_mem_sizze_63824;
        struct memblock indexed_mem_63825;
        
        indexed_mem_63825.references = NULL;
        
        int64_t indexed_mem_sizze_63826;
        struct memblock indexed_mem_63827;
        
        indexed_mem_63827.references = NULL;
        
        int64_t indexed_mem_sizze_63828;
        struct memblock indexed_mem_63829;
        
        indexed_mem_63829.references = NULL;
        
        int64_t xs_mem_sizze_63785;
        struct memblock xs_mem_63786;
        
        xs_mem_63786.references = NULL;
        
        int64_t xs_mem_sizze_63787;
        struct memblock xs_mem_63788;
        
        xs_mem_63788.references = NULL;
        
        int64_t xs_mem_sizze_63789;
        struct memblock xs_mem_63790;
        
        xs_mem_63790.references = NULL;
        xs_mem_sizze_63785 = res_mem_sizze_63779;
        if (memblock_set(ctx, &xs_mem_63786, &res_mem_63780, "res_mem_63780") !=
            0)
            return 1;
        xs_mem_sizze_63787 = res_mem_sizze_63781;
        if (memblock_set(ctx, &xs_mem_63788, &res_mem_63782, "res_mem_63782") !=
            0)
            return 1;
        xs_mem_sizze_63789 = res_mem_sizze_63783;
        if (memblock_set(ctx, &xs_mem_63790, &res_mem_63784, "res_mem_63784") !=
            0)
            return 1;
        for (int32_t i_59727 = 0; i_59727 < res_59596; i_59727++) {
            int32_t upper_bound_59728 = 1 + i_59727;
            int64_t res_mem_sizze_63818;
            struct memblock res_mem_63819;
            
            res_mem_63819.references = NULL;
            
            int64_t res_mem_sizze_63820;
            struct memblock res_mem_63821;
            
            res_mem_63821.references = NULL;
            
            int64_t res_mem_sizze_63822;
            struct memblock res_mem_63823;
            
            res_mem_63823.references = NULL;
            
            int64_t xs_mem_sizze_63791;
            struct memblock xs_mem_63792;
            
            xs_mem_63792.references = NULL;
            
            int64_t xs_mem_sizze_63793;
            struct memblock xs_mem_63794;
            
            xs_mem_63794.references = NULL;
            
            int64_t xs_mem_sizze_63795;
            struct memblock xs_mem_63796;
            
            xs_mem_63796.references = NULL;
            xs_mem_sizze_63791 = xs_mem_sizze_63785;
            if (memblock_set(ctx, &xs_mem_63792, &xs_mem_63786,
                             "xs_mem_63786") != 0)
                return 1;
            xs_mem_sizze_63793 = xs_mem_sizze_63787;
            if (memblock_set(ctx, &xs_mem_63794, &xs_mem_63788,
                             "xs_mem_63788") != 0)
                return 1;
            xs_mem_sizze_63795 = xs_mem_sizze_63789;
            if (memblock_set(ctx, &xs_mem_63796, &xs_mem_63790,
                             "xs_mem_63790") != 0)
                return 1;
            for (int32_t j_59739 = 0; j_59739 < upper_bound_59728; j_59739++) {
                int32_t y_59740 = i_59727 - j_59739;
                int32_t res_59741 = 1 << y_59740;
                struct memblock mem_63799;
                
                mem_63799.references = NULL;
                if (memblock_alloc(ctx, &mem_63799, bytes_63797, "mem_63799"))
                    return 1;
                
                struct memblock mem_63802;
                
                mem_63802.references = NULL;
                if (memblock_alloc(ctx, &mem_63802, bytes_63797, "mem_63802"))
                    return 1;
                
                struct memblock mem_63805;
                
                mem_63805.references = NULL;
                if (memblock_alloc(ctx, &mem_63805, bytes_63797, "mem_63805"))
                    return 1;
                for (int32_t i_61539 = 0; i_61539 < sizze_59590; i_61539++) {
                    int32_t res_59746 = *(int32_t *) &xs_mem_63792.mem[i_61539 *
                                                                       4];
                    int32_t res_59747 = *(int32_t *) &xs_mem_63794.mem[i_61539 *
                                                                       4];
                    int32_t res_59748 = *(int32_t *) &xs_mem_63796.mem[i_61539 *
                                                                       4];
                    int32_t x_59749 = ashr32(i_61539, i_59727);
                    int32_t x_59750 = 2 & x_59749;
                    bool res_59751 = x_59750 == 0;
                    int32_t x_59752 = res_59741 & i_61539;
                    bool cond_59753 = x_59752 == 0;
                    int32_t res_59754;
                    int32_t res_59755;
                    int32_t res_59756;
                    
                    if (cond_59753) {
                        int32_t i_59757 = res_59741 | i_61539;
                        int32_t res_59758 =
                                *(int32_t *) &xs_mem_63792.mem[i_59757 * 4];
                        int32_t res_59759 =
                                *(int32_t *) &xs_mem_63794.mem[i_59757 * 4];
                        int32_t res_59760 =
                                *(int32_t *) &xs_mem_63796.mem[i_59757 * 4];
                        bool cond_59761 = res_59758 == res_59746;
                        bool res_59762 = sle32(res_59759, res_59747);
                        bool res_59763 = sle32(res_59758, res_59746);
                        bool x_59764 = cond_59761 && res_59762;
                        bool x_59765 = !cond_59761;
                        bool y_59766 = res_59763 && x_59765;
                        bool res_59767 = x_59764 || y_59766;
                        bool cond_59768 = res_59767 == res_59751;
                        int32_t res_59769;
                        
                        if (cond_59768) {
                            res_59769 = res_59758;
                        } else {
                            res_59769 = res_59746;
                        }
                        
                        int32_t res_59770;
                        
                        if (cond_59768) {
                            res_59770 = res_59759;
                        } else {
                            res_59770 = res_59747;
                        }
                        
                        int32_t res_59771;
                        
                        if (cond_59768) {
                            res_59771 = res_59760;
                        } else {
                            res_59771 = res_59748;
                        }
                        res_59754 = res_59769;
                        res_59755 = res_59770;
                        res_59756 = res_59771;
                    } else {
                        int32_t i_59772 = res_59741 ^ i_61539;
                        int32_t res_59773 =
                                *(int32_t *) &xs_mem_63792.mem[i_59772 * 4];
                        int32_t res_59774 =
                                *(int32_t *) &xs_mem_63794.mem[i_59772 * 4];
                        int32_t res_59775 =
                                *(int32_t *) &xs_mem_63796.mem[i_59772 * 4];
                        bool cond_59776 = res_59746 == res_59773;
                        bool res_59777 = sle32(res_59747, res_59774);
                        bool res_59778 = sle32(res_59746, res_59773);
                        bool x_59779 = cond_59776 && res_59777;
                        bool x_59780 = !cond_59776;
                        bool y_59781 = res_59778 && x_59780;
                        bool res_59782 = x_59779 || y_59781;
                        bool cond_59783 = res_59782 == res_59751;
                        int32_t res_59784;
                        
                        if (cond_59783) {
                            res_59784 = res_59773;
                        } else {
                            res_59784 = res_59746;
                        }
                        
                        int32_t res_59785;
                        
                        if (cond_59783) {
                            res_59785 = res_59774;
                        } else {
                            res_59785 = res_59747;
                        }
                        
                        int32_t res_59786;
                        
                        if (cond_59783) {
                            res_59786 = res_59775;
                        } else {
                            res_59786 = res_59748;
                        }
                        res_59754 = res_59784;
                        res_59755 = res_59785;
                        res_59756 = res_59786;
                    }
                    *(int32_t *) &mem_63799.mem[i_61539 * 4] = res_59754;
                    *(int32_t *) &mem_63802.mem[i_61539 * 4] = res_59755;
                    *(int32_t *) &mem_63805.mem[i_61539 * 4] = res_59756;
                }
                
                int64_t xs_mem_sizze_tmp_64516 = bytes_63797;
                struct memblock xs_mem_tmp_64517;
                
                xs_mem_tmp_64517.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64517, &mem_63799,
                                 "mem_63799") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_64518 = bytes_63797;
                struct memblock xs_mem_tmp_64519;
                
                xs_mem_tmp_64519.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64519, &mem_63802,
                                 "mem_63802") != 0)
                    return 1;
                
                int64_t xs_mem_sizze_tmp_64520 = bytes_63797;
                struct memblock xs_mem_tmp_64521;
                
                xs_mem_tmp_64521.references = NULL;
                if (memblock_set(ctx, &xs_mem_tmp_64521, &mem_63805,
                                 "mem_63805") != 0)
                    return 1;
                xs_mem_sizze_63791 = xs_mem_sizze_tmp_64516;
                if (memblock_set(ctx, &xs_mem_63792, &xs_mem_tmp_64517,
                                 "xs_mem_tmp_64517") != 0)
                    return 1;
                xs_mem_sizze_63793 = xs_mem_sizze_tmp_64518;
                if (memblock_set(ctx, &xs_mem_63794, &xs_mem_tmp_64519,
                                 "xs_mem_tmp_64519") != 0)
                    return 1;
                xs_mem_sizze_63795 = xs_mem_sizze_tmp_64520;
                if (memblock_set(ctx, &xs_mem_63796, &xs_mem_tmp_64521,
                                 "xs_mem_tmp_64521") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64521,
                                   "xs_mem_tmp_64521") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64519,
                                   "xs_mem_tmp_64519") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_tmp_64517,
                                   "xs_mem_tmp_64517") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63805, "mem_63805") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63802, "mem_63802") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63799, "mem_63799") != 0)
                    return 1;
            }
            res_mem_sizze_63818 = xs_mem_sizze_63791;
            if (memblock_set(ctx, &res_mem_63819, &xs_mem_63792,
                             "xs_mem_63792") != 0)
                return 1;
            res_mem_sizze_63820 = xs_mem_sizze_63793;
            if (memblock_set(ctx, &res_mem_63821, &xs_mem_63794,
                             "xs_mem_63794") != 0)
                return 1;
            res_mem_sizze_63822 = xs_mem_sizze_63795;
            if (memblock_set(ctx, &res_mem_63823, &xs_mem_63796,
                             "xs_mem_63796") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64507 = res_mem_sizze_63818;
            struct memblock xs_mem_tmp_64508;
            
            xs_mem_tmp_64508.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64508, &res_mem_63819,
                             "res_mem_63819") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64509 = res_mem_sizze_63820;
            struct memblock xs_mem_tmp_64510;
            
            xs_mem_tmp_64510.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64510, &res_mem_63821,
                             "res_mem_63821") != 0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64511 = res_mem_sizze_63822;
            struct memblock xs_mem_tmp_64512;
            
            xs_mem_tmp_64512.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64512, &res_mem_63823,
                             "res_mem_63823") != 0)
                return 1;
            xs_mem_sizze_63785 = xs_mem_sizze_tmp_64507;
            if (memblock_set(ctx, &xs_mem_63786, &xs_mem_tmp_64508,
                             "xs_mem_tmp_64508") != 0)
                return 1;
            xs_mem_sizze_63787 = xs_mem_sizze_tmp_64509;
            if (memblock_set(ctx, &xs_mem_63788, &xs_mem_tmp_64510,
                             "xs_mem_tmp_64510") != 0)
                return 1;
            xs_mem_sizze_63789 = xs_mem_sizze_tmp_64511;
            if (memblock_set(ctx, &xs_mem_63790, &xs_mem_tmp_64512,
                             "xs_mem_tmp_64512") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64512, "xs_mem_tmp_64512") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64510, "xs_mem_tmp_64510") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64508, "xs_mem_tmp_64508") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63796, "xs_mem_63796") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63794, "xs_mem_63794") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63792, "xs_mem_63792") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63823, "res_mem_63823") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63821, "res_mem_63821") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63819, "res_mem_63819") != 0)
                return 1;
        }
        indexed_mem_sizze_63824 = xs_mem_sizze_63785;
        if (memblock_set(ctx, &indexed_mem_63825, &xs_mem_63786,
                         "xs_mem_63786") != 0)
            return 1;
        indexed_mem_sizze_63826 = xs_mem_sizze_63787;
        if (memblock_set(ctx, &indexed_mem_63827, &xs_mem_63788,
                         "xs_mem_63788") != 0)
            return 1;
        indexed_mem_sizze_63828 = xs_mem_sizze_63789;
        if (memblock_set(ctx, &indexed_mem_63829, &xs_mem_63790,
                         "xs_mem_63790") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
            return 1;
        
        int32_t m_59787 = conc_tmp_59584 - 1;
        bool zzero_leq_i_p_m_t_s_59788 = sle32(0, m_59787);
        bool i_p_m_t_s_leq_w_59789 = slt32(m_59787, sizze_59590);
        bool y_59791 = zzero_leq_i_p_m_t_s_59788 && i_p_m_t_s_leq_w_59789;
        bool ok_or_empty_59793 = cond_59588 || y_59791;
        bool index_certs_59794;
        
        if (!ok_or_empty_59793) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:178:14-92 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                                   "Index [", "", ":", conc_tmp_59584,
                                   "] out of bounds for array of shape [",
                                   sizze_59590, "].");
            if (memblock_unref(ctx, &xs_mem_63790, "xs_mem_63790") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63788, "xs_mem_63788") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63786, "xs_mem_63786") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63829, "indexed_mem_63829") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63827, "indexed_mem_63827") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63825, "indexed_mem_63825") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        struct memblock mem_63832;
        
        mem_63832.references = NULL;
        if (memblock_alloc(ctx, &mem_63832, bytes_63728, "mem_63832"))
            return 1;
        
        struct memblock mem_63834;
        
        mem_63834.references = NULL;
        if (memblock_alloc(ctx, &mem_63834, binop_x_63729, "mem_63834"))
            return 1;
        
        int32_t discard_61566;
        int32_t scanacc_61551 = 1;
        
        for (int32_t i_61557 = 0; i_61557 < conc_tmp_59584; i_61557++) {
            int32_t x_59816 = *(int32_t *) &indexed_mem_63825.mem[i_61557 * 4];
            int32_t x_59817 = *(int32_t *) &indexed_mem_63827.mem[i_61557 * 4];
            int32_t i_p_o_62337 = -1 + i_61557;
            int32_t rot_i_62338 = smod32(i_p_o_62337, conc_tmp_59584);
            int32_t x_59818 = *(int32_t *) &indexed_mem_63825.mem[rot_i_62338 *
                                                                  4];
            int32_t x_59819 = *(int32_t *) &indexed_mem_63827.mem[rot_i_62338 *
                                                                  4];
            int32_t x_59820 = *(int32_t *) &indexed_mem_63829.mem[i_61557 * 4];
            bool res_59821 = x_59816 == x_59818;
            bool res_59822 = x_59817 == x_59819;
            bool eq_59823 = res_59821 && res_59822;
            bool res_59824 = !eq_59823;
            int32_t res_59814;
            
            if (res_59824) {
                res_59814 = x_59820;
            } else {
                int32_t res_59815 = x_59820 * scanacc_61551;
                
                res_59814 = res_59815;
            }
            *(int32_t *) &mem_63832.mem[i_61557 * 4] = res_59814;
            *(bool *) &mem_63834.mem[i_61557] = res_59824;
            
            int32_t scanacc_tmp_64528 = res_59814;
            
            scanacc_61551 = scanacc_tmp_64528;
        }
        discard_61566 = scanacc_61551;
        if (memblock_unref(ctx, &indexed_mem_63825, "indexed_mem_63825") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63827, "indexed_mem_63827") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63829, "indexed_mem_63829") != 0)
            return 1;
        
        struct memblock mem_63845;
        
        mem_63845.references = NULL;
        if (memblock_alloc(ctx, &mem_63845, bytes_63728, "mem_63845"))
            return 1;
        
        int32_t discard_61572;
        int32_t scanacc_61568 = 0;
        
        for (int32_t i_61570 = 0; i_61570 < conc_tmp_59584; i_61570++) {
            int32_t i_p_o_62345 = 1 + i_61570;
            int32_t rot_i_62346 = smod32(i_p_o_62345, conc_tmp_59584);
            bool x_59830 = *(bool *) &mem_63834.mem[rot_i_62346];
            int32_t res_59831 = btoi_bool_i32(x_59830);
            int32_t res_59829 = res_59831 + scanacc_61568;
            
            *(int32_t *) &mem_63845.mem[i_61570 * 4] = res_59829;
            
            int32_t scanacc_tmp_64531 = res_59829;
            
            scanacc_61568 = scanacc_tmp_64531;
        }
        discard_61572 = scanacc_61568;
        
        int32_t res_59832;
        
        if (loop_cond_59589) {
            bool index_certs_59835;
            
            if (!zzero_leq_i_p_m_t_s_59788) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                       "Index [", m_59787,
                                       "] out of bounds for array of shape [",
                                       conc_tmp_59584, "].");
                if (memblock_unref(ctx, &mem_63845, "mem_63845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63834, "mem_63834") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63832, "mem_63832") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63790, "xs_mem_63790") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63788, "xs_mem_63788") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63786, "xs_mem_63786") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63829,
                                   "indexed_mem_63829") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63827,
                                   "indexed_mem_63827") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63825,
                                   "indexed_mem_63825") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63167,
                                   "indexed_mem_63167") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63165,
                                   "indexed_mem_63165") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63163,
                                   "indexed_mem_63163") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int32_t res_59836 = *(int32_t *) &mem_63845.mem[m_59787 * 4];
            
            res_59832 = res_59836;
        } else {
            res_59832 = 0;
        }
        
        bool bounds_invalid_upwards_59837 = slt32(res_59832, 0);
        bool eq_x_zz_59838 = 0 == res_59832;
        bool not_p_59839 = !bounds_invalid_upwards_59837;
        bool p_and_eq_x_y_59840 = eq_x_zz_59838 && not_p_59839;
        bool dim_zzero_59841 = bounds_invalid_upwards_59837 ||
             p_and_eq_x_y_59840;
        bool both_empty_59842 = eq_x_zz_59838 && dim_zzero_59841;
        bool eq_x_y_59843 = res_59832 == 0;
        bool empty_or_match_59846 = not_p_59839 || both_empty_59842;
        bool empty_or_match_cert_59847;
        
        if (!empty_or_match_59846) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:179:44-49 -> tupleTest.fut:159:13-45 -> tupleSparse.fut:180:13-86 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                                   "Function return value does not match shape of type ",
                                   "*", "[", res_59832, "]", "t");
            if (memblock_unref(ctx, &mem_63845, "mem_63845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63834, "mem_63834") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63832, "mem_63832") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63790, "xs_mem_63790") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63788, "xs_mem_63788") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63786, "xs_mem_63786") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63829, "indexed_mem_63829") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63827, "indexed_mem_63827") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63825, "indexed_mem_63825") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int64_t binop_x_63851 = sext_i32_i64(res_59832);
        int64_t bytes_63850 = 4 * binop_x_63851;
        struct memblock mem_63852;
        
        mem_63852.references = NULL;
        if (memblock_alloc(ctx, &mem_63852, bytes_63850, "mem_63852"))
            return 1;
        for (int32_t i_64533 = 0; i_64533 < res_59832; i_64533++) {
            *(int32_t *) &mem_63852.mem[i_64533 * 4] = 1;
        }
        for (int32_t write_iter_61573 = 0; write_iter_61573 < conc_tmp_59584;
             write_iter_61573++) {
            int32_t write_iv_61577 =
                    *(int32_t *) &mem_63845.mem[write_iter_61573 * 4];
            int32_t i_p_o_62350 = 1 + write_iter_61573;
            int32_t rot_i_62351 = smod32(i_p_o_62350, conc_tmp_59584);
            bool write_iv_61578 = *(bool *) &mem_63834.mem[rot_i_62351];
            int32_t res_59859;
            
            if (write_iv_61578) {
                int32_t res_59860 = write_iv_61577 - 1;
                
                res_59859 = res_59860;
            } else {
                res_59859 = -1;
            }
            
            bool less_than_zzero_61594 = slt32(res_59859, 0);
            bool greater_than_sizze_61595 = sle32(res_59832, res_59859);
            bool outside_bounds_dim_61596 = less_than_zzero_61594 ||
                 greater_than_sizze_61595;
            
            if (!outside_bounds_dim_61596) {
                memmove(mem_63852.mem + res_59859 * 4, mem_63832.mem +
                        write_iter_61573 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63832, "mem_63832") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63834, "mem_63834") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63845, "mem_63845") != 0)
            return 1;
        
        struct memblock mem_63859;
        
        mem_63859.references = NULL;
        if (memblock_alloc(ctx, &mem_63859, bytes_63850, "mem_63859"))
            return 1;
        
        struct memblock mem_63862;
        
        mem_63862.references = NULL;
        if (memblock_alloc(ctx, &mem_63862, bytes_63850, "mem_63862"))
            return 1;
        
        int32_t discard_61608;
        int32_t scanacc_61602 = 0;
        
        for (int32_t i_61605 = 0; i_61605 < res_59832; i_61605++) {
            int32_t x_59866 = *(int32_t *) &mem_63852.mem[i_61605 * 4];
            bool not_arg_59867 = x_59866 == 0;
            bool res_59868 = !not_arg_59867;
            int32_t part_res_59869;
            
            if (res_59868) {
                part_res_59869 = 0;
            } else {
                part_res_59869 = 1;
            }
            
            int32_t part_res_59870;
            
            if (res_59868) {
                part_res_59870 = 1;
            } else {
                part_res_59870 = 0;
            }
            
            int32_t zz_59865 = part_res_59870 + scanacc_61602;
            
            *(int32_t *) &mem_63859.mem[i_61605 * 4] = zz_59865;
            *(int32_t *) &mem_63862.mem[i_61605 * 4] = part_res_59869;
            
            int32_t scanacc_tmp_64535 = zz_59865;
            
            scanacc_61602 = scanacc_tmp_64535;
        }
        discard_61608 = scanacc_61602;
        
        int32_t last_index_59871 = res_59832 - 1;
        int32_t partition_sizze_59872;
        
        if (eq_x_y_59843) {
            partition_sizze_59872 = 0;
        } else {
            int32_t last_offset_59873 =
                    *(int32_t *) &mem_63859.mem[last_index_59871 * 4];
            
            partition_sizze_59872 = last_offset_59873;
        }
        
        int64_t binop_x_63872 = sext_i32_i64(partition_sizze_59872);
        int64_t bytes_63871 = 4 * binop_x_63872;
        struct memblock mem_63873;
        
        mem_63873.references = NULL;
        if (memblock_alloc(ctx, &mem_63873, bytes_63871, "mem_63873"))
            return 1;
        for (int32_t write_iter_61609 = 0; write_iter_61609 < res_59832;
             write_iter_61609++) {
            int32_t write_iv_61611 =
                    *(int32_t *) &mem_63862.mem[write_iter_61609 * 4];
            int32_t write_iv_61612 =
                    *(int32_t *) &mem_63859.mem[write_iter_61609 * 4];
            bool is_this_one_59881 = write_iv_61611 == 0;
            int32_t this_offset_59882 = -1 + write_iv_61612;
            int32_t total_res_59883;
            
            if (is_this_one_59881) {
                total_res_59883 = this_offset_59882;
            } else {
                total_res_59883 = -1;
            }
            
            bool less_than_zzero_61616 = slt32(total_res_59883, 0);
            bool greater_than_sizze_61617 = sle32(partition_sizze_59872,
                                                  total_res_59883);
            bool outside_bounds_dim_61618 = less_than_zzero_61616 ||
                 greater_than_sizze_61617;
            
            if (!outside_bounds_dim_61618) {
                memmove(mem_63873.mem + total_res_59883 * 4, mem_63852.mem +
                        write_iter_61609 * 4, sizeof(int32_t));
            }
        }
        if (memblock_unref(ctx, &mem_63852, "mem_63852") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63859, "mem_63859") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63862, "mem_63862") != 0)
            return 1;
        
        bool cond_59884 = partition_sizze_59872 == 4;
        bool res_59885;
        
        if (cond_59884) {
            bool res_59886;
            bool redout_61622 = 1;
            
            for (int32_t i_61623 = 0; i_61623 < partition_sizze_59872;
                 i_61623++) {
                int32_t x_59890 = *(int32_t *) &mem_63873.mem[i_61623 * 4];
                bool res_59891 = x_59890 == 1;
                bool x_59889 = res_59891 && redout_61622;
                bool redout_tmp_64539 = x_59889;
                
                redout_61622 = redout_tmp_64539;
            }
            res_59886 = redout_61622;
            res_59885 = res_59886;
        } else {
            res_59885 = 0;
        }
        if (memblock_unref(ctx, &mem_63873, "mem_63873") != 0)
            return 1;
        res_59501 = res_59885;
        if (memblock_unref(ctx, &mem_63873, "mem_63873") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63862, "mem_63862") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63859, "mem_63859") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63852, "mem_63852") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63845, "mem_63845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63834, "mem_63834") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63832, "mem_63832") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63790, "xs_mem_63790") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63788, "xs_mem_63788") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63786, "xs_mem_63786") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63829, "indexed_mem_63829") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63827, "indexed_mem_63827") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63825, "indexed_mem_63825") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63784, "res_mem_63784") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63782, "res_mem_63782") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63780, "res_mem_63780") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63736, "mem_63736") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63733, "mem_63733") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63730, "mem_63730") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63712, "mem_63712") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63709, "mem_63709") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63706, "mem_63706") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63695, "mem_63695") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63692, "mem_63692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63687, "mem_63687") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63682, "mem_63682") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63664, "mem_63664") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63661, "mem_63661") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63658, "mem_63658") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63639, "mem_63639") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63636, "mem_63636") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63633, "mem_63633") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63630, "mem_63630") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63625, "mem_63625") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63620, "mem_63620") != 0)
            return 1;
    } else {
        res_59501 = 0;
    }
    
    struct memblock mem_63880;
    
    mem_63880.references = NULL;
    if (memblock_alloc(ctx, &mem_63880, 24, "mem_63880"))
        return 1;
    
    struct memblock mem_63885;
    
    mem_63885.references = NULL;
    if (memblock_alloc(ctx, &mem_63885, 8, "mem_63885"))
        return 1;
    for (int32_t i_61626 = 0; i_61626 < 3; i_61626++) {
        for (int32_t i_64541 = 0; i_64541 < 2; i_64541++) {
            *(int32_t *) &mem_63885.mem[i_64541 * 4] = i_61626;
        }
        memmove(mem_63880.mem + 2 * i_61626 * 4, mem_63885.mem + 0, 2 *
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
        return 1;
    
    struct memblock mem_63890;
    
    mem_63890.references = NULL;
    if (memblock_alloc(ctx, &mem_63890, 24, "mem_63890"))
        return 1;
    
    struct memblock mem_63893;
    
    mem_63893.references = NULL;
    if (memblock_alloc(ctx, &mem_63893, 24, "mem_63893"))
        return 1;
    
    int32_t discard_61636;
    int32_t scanacc_61630 = 0;
    
    for (int32_t i_61633 = 0; i_61633 < 6; i_61633++) {
        bool not_arg_59902 = i_61633 == 0;
        bool res_59903 = !not_arg_59902;
        int32_t part_res_59904;
        
        if (res_59903) {
            part_res_59904 = 0;
        } else {
            part_res_59904 = 1;
        }
        
        int32_t part_res_59905;
        
        if (res_59903) {
            part_res_59905 = 1;
        } else {
            part_res_59905 = 0;
        }
        
        int32_t zz_59900 = part_res_59905 + scanacc_61630;
        
        *(int32_t *) &mem_63890.mem[i_61633 * 4] = zz_59900;
        *(int32_t *) &mem_63893.mem[i_61633 * 4] = part_res_59904;
        
        int32_t scanacc_tmp_64542 = zz_59900;
        
        scanacc_61630 = scanacc_tmp_64542;
    }
    discard_61636 = scanacc_61630;
    
    int32_t last_offset_59906 = *(int32_t *) &mem_63890.mem[20];
    int64_t binop_x_63903 = sext_i32_i64(last_offset_59906);
    int64_t bytes_63902 = 4 * binop_x_63903;
    struct memblock mem_63904;
    
    mem_63904.references = NULL;
    if (memblock_alloc(ctx, &mem_63904, bytes_63902, "mem_63904"))
        return 1;
    
    struct memblock mem_63907;
    
    mem_63907.references = NULL;
    if (memblock_alloc(ctx, &mem_63907, bytes_63902, "mem_63907"))
        return 1;
    
    struct memblock mem_63910;
    
    mem_63910.references = NULL;
    if (memblock_alloc(ctx, &mem_63910, bytes_63902, "mem_63910"))
        return 1;
    for (int32_t write_iter_61637 = 0; write_iter_61637 < 6;
         write_iter_61637++) {
        int32_t write_iv_61641 = *(int32_t *) &mem_63893.mem[write_iter_61637 *
                                                             4];
        int32_t write_iv_61642 = *(int32_t *) &mem_63890.mem[write_iter_61637 *
                                                             4];
        int32_t new_index_62355 = squot32(write_iter_61637, 2);
        int32_t binop_y_62357 = 2 * new_index_62355;
        int32_t new_index_62358 = write_iter_61637 - binop_y_62357;
        bool is_this_one_59918 = write_iv_61641 == 0;
        int32_t this_offset_59919 = -1 + write_iv_61642;
        int32_t total_res_59920;
        
        if (is_this_one_59918) {
            total_res_59920 = this_offset_59919;
        } else {
            total_res_59920 = -1;
        }
        
        bool less_than_zzero_61646 = slt32(total_res_59920, 0);
        bool greater_than_sizze_61647 = sle32(last_offset_59906,
                                              total_res_59920);
        bool outside_bounds_dim_61648 = less_than_zzero_61646 ||
             greater_than_sizze_61647;
        
        if (!outside_bounds_dim_61648) {
            memmove(mem_63904.mem + total_res_59920 * 4, mem_63880.mem + (2 *
                                                                          new_index_62355 +
                                                                          new_index_62358) *
                    4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_61648) {
            struct memblock mem_63919;
            
            mem_63919.references = NULL;
            if (memblock_alloc(ctx, &mem_63919, 4, "mem_63919"))
                return 1;
            
            int32_t x_64549;
            
            for (int32_t i_64548 = 0; i_64548 < 1; i_64548++) {
                x_64549 = new_index_62358 + sext_i32_i32(i_64548);
                *(int32_t *) &mem_63919.mem[i_64548 * 4] = x_64549;
            }
            memmove(mem_63907.mem + total_res_59920 * 4, mem_63919.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_63919, "mem_63919") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63919, "mem_63919") != 0)
                return 1;
        }
        if (!outside_bounds_dim_61648) {
            struct memblock mem_63922;
            
            mem_63922.references = NULL;
            if (memblock_alloc(ctx, &mem_63922, 4, "mem_63922"))
                return 1;
            
            int32_t x_64551;
            
            for (int32_t i_64550 = 0; i_64550 < 1; i_64550++) {
                x_64551 = write_iter_61637 + sext_i32_i32(i_64550);
                *(int32_t *) &mem_63922.mem[i_64550 * 4] = x_64551;
            }
            memmove(mem_63910.mem + total_res_59920 * 4, mem_63922.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_63922, "mem_63922") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63922, "mem_63922") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
        return 1;
    
    bool empty_slice_59921 = last_offset_59906 == 0;
    int32_t m_59922 = last_offset_59906 - 1;
    bool zzero_leq_i_p_m_t_s_59923 = sle32(0, m_59922);
    bool loop_cond_59930 = slt32(1, last_offset_59906);
    int32_t sizze_59931;
    int32_t sizze_59932;
    int32_t sizze_59933;
    int64_t res_mem_sizze_63971;
    struct memblock res_mem_63972;
    
    res_mem_63972.references = NULL;
    
    int64_t res_mem_sizze_63973;
    struct memblock res_mem_63974;
    
    res_mem_63974.references = NULL;
    
    int64_t res_mem_sizze_63975;
    struct memblock res_mem_63976;
    
    res_mem_63976.references = NULL;
    
    int32_t res_59937;
    
    if (empty_slice_59921) {
        struct memblock mem_63931;
        
        mem_63931.references = NULL;
        if (memblock_alloc(ctx, &mem_63931, bytes_63902, "mem_63931"))
            return 1;
        memmove(mem_63931.mem + 0, mem_63904.mem + 0, last_offset_59906 *
                sizeof(int32_t));
        
        struct memblock mem_63934;
        
        mem_63934.references = NULL;
        if (memblock_alloc(ctx, &mem_63934, bytes_63902, "mem_63934"))
            return 1;
        memmove(mem_63934.mem + 0, mem_63907.mem + 0, last_offset_59906 *
                sizeof(int32_t));
        
        struct memblock mem_63937;
        
        mem_63937.references = NULL;
        if (memblock_alloc(ctx, &mem_63937, bytes_63902, "mem_63937"))
            return 1;
        memmove(mem_63937.mem + 0, mem_63910.mem + 0, last_offset_59906 *
                sizeof(int32_t));
        sizze_59931 = last_offset_59906;
        sizze_59932 = last_offset_59906;
        sizze_59933 = last_offset_59906;
        res_mem_sizze_63971 = bytes_63902;
        if (memblock_set(ctx, &res_mem_63972, &mem_63931, "mem_63931") != 0)
            return 1;
        res_mem_sizze_63973 = bytes_63902;
        if (memblock_set(ctx, &res_mem_63974, &mem_63934, "mem_63934") != 0)
            return 1;
        res_mem_sizze_63975 = bytes_63902;
        if (memblock_set(ctx, &res_mem_63976, &mem_63937, "mem_63937") != 0)
            return 1;
        res_59937 = 0;
        if (memblock_unref(ctx, &mem_63937, "mem_63937") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63934, "mem_63934") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63931, "mem_63931") != 0)
            return 1;
    } else {
        bool res_59949;
        int32_t res_59950;
        int32_t res_59951;
        bool loop_while_59952;
        int32_t r_59953;
        int32_t n_59954;
        
        loop_while_59952 = loop_cond_59930;
        r_59953 = 0;
        n_59954 = last_offset_59906;
        while (loop_while_59952) {
            int32_t res_59955 = sdiv32(n_59954, 2);
            int32_t res_59956 = 1 + r_59953;
            bool loop_cond_59957 = slt32(1, res_59955);
            bool loop_while_tmp_64552 = loop_cond_59957;
            int32_t r_tmp_64553 = res_59956;
            int32_t n_tmp_64554;
            
            n_tmp_64554 = res_59955;
            loop_while_59952 = loop_while_tmp_64552;
            r_59953 = r_tmp_64553;
            n_59954 = n_tmp_64554;
        }
        res_59949 = loop_while_59952;
        res_59950 = r_59953;
        res_59951 = n_59954;
        
        int32_t y_59958 = 1 << res_59950;
        bool cond_59959 = last_offset_59906 == y_59958;
        int32_t y_59960 = 1 + res_59950;
        int32_t x_59961 = 1 << y_59960;
        int32_t arg_59962 = x_59961 - last_offset_59906;
        bool bounds_invalid_upwards_59963 = slt32(arg_59962, 0);
        int32_t conc_tmp_59964 = last_offset_59906 + arg_59962;
        int32_t sizze_59965;
        
        if (cond_59959) {
            sizze_59965 = last_offset_59906;
        } else {
            sizze_59965 = conc_tmp_59964;
        }
        
        int32_t res_59966;
        
        if (cond_59959) {
            res_59966 = res_59950;
        } else {
            res_59966 = y_59960;
        }
        
        int64_t binop_x_63957 = sext_i32_i64(conc_tmp_59964);
        int64_t bytes_63956 = 4 * binop_x_63957;
        int64_t res_mem_sizze_63965;
        struct memblock res_mem_63966;
        
        res_mem_63966.references = NULL;
        
        int64_t res_mem_sizze_63967;
        struct memblock res_mem_63968;
        
        res_mem_63968.references = NULL;
        
        int64_t res_mem_sizze_63969;
        struct memblock res_mem_63970;
        
        res_mem_63970.references = NULL;
        if (cond_59959) {
            struct memblock mem_63940;
            
            mem_63940.references = NULL;
            if (memblock_alloc(ctx, &mem_63940, bytes_63902, "mem_63940"))
                return 1;
            memmove(mem_63940.mem + 0, mem_63904.mem + 0, last_offset_59906 *
                    sizeof(int32_t));
            
            struct memblock mem_63943;
            
            mem_63943.references = NULL;
            if (memblock_alloc(ctx, &mem_63943, bytes_63902, "mem_63943"))
                return 1;
            memmove(mem_63943.mem + 0, mem_63907.mem + 0, last_offset_59906 *
                    sizeof(int32_t));
            
            struct memblock mem_63946;
            
            mem_63946.references = NULL;
            if (memblock_alloc(ctx, &mem_63946, bytes_63902, "mem_63946"))
                return 1;
            memmove(mem_63946.mem + 0, mem_63910.mem + 0, last_offset_59906 *
                    sizeof(int32_t));
            res_mem_sizze_63965 = bytes_63902;
            if (memblock_set(ctx, &res_mem_63966, &mem_63940, "mem_63940") != 0)
                return 1;
            res_mem_sizze_63967 = bytes_63902;
            if (memblock_set(ctx, &res_mem_63968, &mem_63943, "mem_63943") != 0)
                return 1;
            res_mem_sizze_63969 = bytes_63902;
            if (memblock_set(ctx, &res_mem_63970, &mem_63946, "mem_63946") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63946, "mem_63946") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63943, "mem_63943") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63940, "mem_63940") != 0)
                return 1;
        } else {
            bool y_59984 = slt32(0, last_offset_59906);
            bool index_certs_59985;
            
            if (!y_59984) {
                ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:196:15-115 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:20:66-70",
                                       "Index [", 0,
                                       "] out of bounds for array of shape [",
                                       last_offset_59906, "].");
                if (memblock_unref(ctx, &res_mem_63970, "res_mem_63970") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63968, "res_mem_63968") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63966, "res_mem_63966") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63167,
                                   "indexed_mem_63167") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63165,
                                   "indexed_mem_63165") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63163,
                                   "indexed_mem_63163") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int32_t arg_59986 = *(int32_t *) &mem_63904.mem[0];
            int32_t arg_59987 = *(int32_t *) &mem_63907.mem[0];
            int32_t arg_59988 = *(int32_t *) &mem_63910.mem[0];
            int32_t res_59989;
            int32_t res_59990;
            int32_t res_59991;
            int32_t redout_61664;
            int32_t redout_61665;
            int32_t redout_61666;
            
            redout_61664 = arg_59986;
            redout_61665 = arg_59987;
            redout_61666 = arg_59988;
            for (int32_t i_61667 = 0; i_61667 < last_offset_59906; i_61667++) {
                int32_t x_60008 = *(int32_t *) &mem_63904.mem[i_61667 * 4];
                int32_t x_60009 = *(int32_t *) &mem_63907.mem[i_61667 * 4];
                int32_t x_60010 = *(int32_t *) &mem_63910.mem[i_61667 * 4];
                bool cond_59998 = redout_61664 == x_60008;
                bool res_59999 = sle32(redout_61665, x_60009);
                bool res_60000 = sle32(redout_61664, x_60008);
                bool x_60001 = cond_59998 && res_59999;
                bool x_60002 = !cond_59998;
                bool y_60003 = res_60000 && x_60002;
                bool res_60004 = x_60001 || y_60003;
                int32_t res_60005;
                
                if (res_60004) {
                    res_60005 = x_60008;
                } else {
                    res_60005 = redout_61664;
                }
                
                int32_t res_60006;
                
                if (res_60004) {
                    res_60006 = x_60009;
                } else {
                    res_60006 = redout_61665;
                }
                
                int32_t res_60007;
                
                if (res_60004) {
                    res_60007 = x_60010;
                } else {
                    res_60007 = redout_61666;
                }
                
                int32_t redout_tmp_64555 = res_60005;
                int32_t redout_tmp_64556 = res_60006;
                int32_t redout_tmp_64557;
                
                redout_tmp_64557 = res_60007;
                redout_61664 = redout_tmp_64555;
                redout_61665 = redout_tmp_64556;
                redout_61666 = redout_tmp_64557;
            }
            res_59989 = redout_61664;
            res_59990 = redout_61665;
            res_59991 = redout_61666;
            
            bool eq_x_zz_60011 = 0 == arg_59962;
            bool not_p_60012 = !bounds_invalid_upwards_59963;
            bool p_and_eq_x_y_60013 = eq_x_zz_60011 && not_p_60012;
            bool dim_zzero_60014 = bounds_invalid_upwards_59963 ||
                 p_and_eq_x_y_60013;
            bool both_empty_60015 = eq_x_zz_60011 && dim_zzero_60014;
            bool eq_x_y_60016 = arg_59962 == 0;
            bool p_and_eq_x_y_60017 = bounds_invalid_upwards_59963 &&
                 eq_x_y_60016;
            bool dim_match_60018 = not_p_60012 || p_and_eq_x_y_60017;
            bool empty_or_match_60019 = both_empty_60015 || dim_match_60018;
            bool empty_or_match_cert_60020;
            
            if (!empty_or_match_60019) {
                ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                       "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:196:15-115 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> lib/github.com/diku-dk/sorts/merge_sort.fut:21:26-57 -> /futlib/array.fut:66:1-67:19",
                                       "Function return value does not match shape of type ",
                                       "*", "[", arg_59962, "]", "t");
                if (memblock_unref(ctx, &res_mem_63970, "res_mem_63970") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63968, "res_mem_63968") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63966, "res_mem_63966") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                    return 1;
                if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63167,
                                   "indexed_mem_63167") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63165,
                                   "indexed_mem_63165") != 0)
                    return 1;
                if (memblock_unref(ctx, &indexed_mem_63163,
                                   "indexed_mem_63163") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                    return 1;
                if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                    return 1;
                if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                    return 1;
                return 1;
            }
            
            int64_t binop_x_63948 = sext_i32_i64(arg_59962);
            int64_t bytes_63947 = 4 * binop_x_63948;
            struct memblock mem_63949;
            
            mem_63949.references = NULL;
            if (memblock_alloc(ctx, &mem_63949, bytes_63947, "mem_63949"))
                return 1;
            for (int32_t i_64558 = 0; i_64558 < arg_59962; i_64558++) {
                *(int32_t *) &mem_63949.mem[i_64558 * 4] = res_59989;
            }
            
            struct memblock mem_63952;
            
            mem_63952.references = NULL;
            if (memblock_alloc(ctx, &mem_63952, bytes_63947, "mem_63952"))
                return 1;
            for (int32_t i_64559 = 0; i_64559 < arg_59962; i_64559++) {
                *(int32_t *) &mem_63952.mem[i_64559 * 4] = res_59990;
            }
            
            struct memblock mem_63955;
            
            mem_63955.references = NULL;
            if (memblock_alloc(ctx, &mem_63955, bytes_63947, "mem_63955"))
                return 1;
            for (int32_t i_64560 = 0; i_64560 < arg_59962; i_64560++) {
                *(int32_t *) &mem_63955.mem[i_64560 * 4] = res_59991;
            }
            
            struct memblock mem_63958;
            
            mem_63958.references = NULL;
            if (memblock_alloc(ctx, &mem_63958, bytes_63956, "mem_63958"))
                return 1;
            
            int32_t tmp_offs_64561 = 0;
            
            memmove(mem_63958.mem + tmp_offs_64561 * 4, mem_63904.mem + 0,
                    last_offset_59906 * sizeof(int32_t));
            tmp_offs_64561 += last_offset_59906;
            memmove(mem_63958.mem + tmp_offs_64561 * 4, mem_63949.mem + 0,
                    arg_59962 * sizeof(int32_t));
            tmp_offs_64561 += arg_59962;
            if (memblock_unref(ctx, &mem_63949, "mem_63949") != 0)
                return 1;
            
            struct memblock mem_63961;
            
            mem_63961.references = NULL;
            if (memblock_alloc(ctx, &mem_63961, bytes_63956, "mem_63961"))
                return 1;
            
            int32_t tmp_offs_64562 = 0;
            
            memmove(mem_63961.mem + tmp_offs_64562 * 4, mem_63907.mem + 0,
                    last_offset_59906 * sizeof(int32_t));
            tmp_offs_64562 += last_offset_59906;
            memmove(mem_63961.mem + tmp_offs_64562 * 4, mem_63952.mem + 0,
                    arg_59962 * sizeof(int32_t));
            tmp_offs_64562 += arg_59962;
            if (memblock_unref(ctx, &mem_63952, "mem_63952") != 0)
                return 1;
            
            struct memblock mem_63964;
            
            mem_63964.references = NULL;
            if (memblock_alloc(ctx, &mem_63964, bytes_63956, "mem_63964"))
                return 1;
            
            int32_t tmp_offs_64563 = 0;
            
            memmove(mem_63964.mem + tmp_offs_64563 * 4, mem_63910.mem + 0,
                    last_offset_59906 * sizeof(int32_t));
            tmp_offs_64563 += last_offset_59906;
            memmove(mem_63964.mem + tmp_offs_64563 * 4, mem_63955.mem + 0,
                    arg_59962 * sizeof(int32_t));
            tmp_offs_64563 += arg_59962;
            if (memblock_unref(ctx, &mem_63955, "mem_63955") != 0)
                return 1;
            res_mem_sizze_63965 = bytes_63956;
            if (memblock_set(ctx, &res_mem_63966, &mem_63958, "mem_63958") != 0)
                return 1;
            res_mem_sizze_63967 = bytes_63956;
            if (memblock_set(ctx, &res_mem_63968, &mem_63961, "mem_63961") != 0)
                return 1;
            res_mem_sizze_63969 = bytes_63956;
            if (memblock_set(ctx, &res_mem_63970, &mem_63964, "mem_63964") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63964, "mem_63964") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63961, "mem_63961") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63958, "mem_63958") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63955, "mem_63955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63952, "mem_63952") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63949, "mem_63949") != 0)
                return 1;
        }
        sizze_59931 = sizze_59965;
        sizze_59932 = sizze_59965;
        sizze_59933 = sizze_59965;
        res_mem_sizze_63971 = res_mem_sizze_63965;
        if (memblock_set(ctx, &res_mem_63972, &res_mem_63966,
                         "res_mem_63966") != 0)
            return 1;
        res_mem_sizze_63973 = res_mem_sizze_63967;
        if (memblock_set(ctx, &res_mem_63974, &res_mem_63968,
                         "res_mem_63968") != 0)
            return 1;
        res_mem_sizze_63975 = res_mem_sizze_63969;
        if (memblock_set(ctx, &res_mem_63976, &res_mem_63970,
                         "res_mem_63970") != 0)
            return 1;
        res_59937 = res_59966;
        if (memblock_unref(ctx, &res_mem_63970, "res_mem_63970") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63968, "res_mem_63968") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63966, "res_mem_63966") != 0)
            return 1;
    }
    if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
        return 1;
    
    bool dim_zzero_60030 = 0 == sizze_59932;
    bool dim_zzero_60031 = 0 == sizze_59931;
    bool both_empty_60032 = dim_zzero_60030 && dim_zzero_60031;
    bool dim_match_60033 = sizze_59931 == sizze_59932;
    bool empty_or_match_60034 = both_empty_60032 || dim_match_60033;
    bool empty_or_match_cert_60035;
    
    if (!empty_or_match_60034) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:196:15-115 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    bool dim_zzero_60037 = 0 == sizze_59933;
    bool both_empty_60038 = dim_zzero_60031 && dim_zzero_60037;
    bool dim_match_60039 = sizze_59931 == sizze_59933;
    bool empty_or_match_60040 = both_empty_60038 || dim_match_60039;
    bool empty_or_match_cert_60041;
    
    if (!empty_or_match_60040) {
        ctx->error = msgprintf("Error at %s:\n%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:196:15-115 -> lib/github.com/diku-dk/sorts/merge_sort.fut:45:17-36 -> unknown location",
                               "Function return value does not match shape of declared return type.");
        if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    int64_t binop_x_63990 = sext_i32_i64(sizze_59931);
    int64_t bytes_63989 = 4 * binop_x_63990;
    int64_t indexed_mem_sizze_64016;
    struct memblock indexed_mem_64017;
    
    indexed_mem_64017.references = NULL;
    
    int64_t indexed_mem_sizze_64018;
    struct memblock indexed_mem_64019;
    
    indexed_mem_64019.references = NULL;
    
    int64_t indexed_mem_sizze_64020;
    struct memblock indexed_mem_64021;
    
    indexed_mem_64021.references = NULL;
    
    int64_t xs_mem_sizze_63977;
    struct memblock xs_mem_63978;
    
    xs_mem_63978.references = NULL;
    
    int64_t xs_mem_sizze_63979;
    struct memblock xs_mem_63980;
    
    xs_mem_63980.references = NULL;
    
    int64_t xs_mem_sizze_63981;
    struct memblock xs_mem_63982;
    
    xs_mem_63982.references = NULL;
    xs_mem_sizze_63977 = res_mem_sizze_63971;
    if (memblock_set(ctx, &xs_mem_63978, &res_mem_63972, "res_mem_63972") != 0)
        return 1;
    xs_mem_sizze_63979 = res_mem_sizze_63973;
    if (memblock_set(ctx, &xs_mem_63980, &res_mem_63974, "res_mem_63974") != 0)
        return 1;
    xs_mem_sizze_63981 = res_mem_sizze_63975;
    if (memblock_set(ctx, &xs_mem_63982, &res_mem_63976, "res_mem_63976") != 0)
        return 1;
    for (int32_t i_60058 = 0; i_60058 < res_59937; i_60058++) {
        int32_t upper_bound_60059 = 1 + i_60058;
        int64_t res_mem_sizze_64010;
        struct memblock res_mem_64011;
        
        res_mem_64011.references = NULL;
        
        int64_t res_mem_sizze_64012;
        struct memblock res_mem_64013;
        
        res_mem_64013.references = NULL;
        
        int64_t res_mem_sizze_64014;
        struct memblock res_mem_64015;
        
        res_mem_64015.references = NULL;
        
        int64_t xs_mem_sizze_63983;
        struct memblock xs_mem_63984;
        
        xs_mem_63984.references = NULL;
        
        int64_t xs_mem_sizze_63985;
        struct memblock xs_mem_63986;
        
        xs_mem_63986.references = NULL;
        
        int64_t xs_mem_sizze_63987;
        struct memblock xs_mem_63988;
        
        xs_mem_63988.references = NULL;
        xs_mem_sizze_63983 = xs_mem_sizze_63977;
        if (memblock_set(ctx, &xs_mem_63984, &xs_mem_63978, "xs_mem_63978") !=
            0)
            return 1;
        xs_mem_sizze_63985 = xs_mem_sizze_63979;
        if (memblock_set(ctx, &xs_mem_63986, &xs_mem_63980, "xs_mem_63980") !=
            0)
            return 1;
        xs_mem_sizze_63987 = xs_mem_sizze_63981;
        if (memblock_set(ctx, &xs_mem_63988, &xs_mem_63982, "xs_mem_63982") !=
            0)
            return 1;
        for (int32_t j_60070 = 0; j_60070 < upper_bound_60059; j_60070++) {
            int32_t y_60071 = i_60058 - j_60070;
            int32_t res_60072 = 1 << y_60071;
            struct memblock mem_63991;
            
            mem_63991.references = NULL;
            if (memblock_alloc(ctx, &mem_63991, bytes_63989, "mem_63991"))
                return 1;
            
            struct memblock mem_63994;
            
            mem_63994.references = NULL;
            if (memblock_alloc(ctx, &mem_63994, bytes_63989, "mem_63994"))
                return 1;
            
            struct memblock mem_63997;
            
            mem_63997.references = NULL;
            if (memblock_alloc(ctx, &mem_63997, bytes_63989, "mem_63997"))
                return 1;
            for (int32_t i_61674 = 0; i_61674 < sizze_59931; i_61674++) {
                int32_t res_60077 = *(int32_t *) &xs_mem_63984.mem[i_61674 * 4];
                int32_t res_60078 = *(int32_t *) &xs_mem_63986.mem[i_61674 * 4];
                int32_t res_60079 = *(int32_t *) &xs_mem_63988.mem[i_61674 * 4];
                int32_t x_60080 = ashr32(i_61674, i_60058);
                int32_t x_60081 = 2 & x_60080;
                bool res_60082 = x_60081 == 0;
                int32_t x_60083 = res_60072 & i_61674;
                bool cond_60084 = x_60083 == 0;
                int32_t res_60085;
                int32_t res_60086;
                int32_t res_60087;
                
                if (cond_60084) {
                    int32_t i_60088 = res_60072 | i_61674;
                    int32_t res_60089 = *(int32_t *) &xs_mem_63984.mem[i_60088 *
                                                                       4];
                    int32_t res_60090 = *(int32_t *) &xs_mem_63986.mem[i_60088 *
                                                                       4];
                    int32_t res_60091 = *(int32_t *) &xs_mem_63988.mem[i_60088 *
                                                                       4];
                    bool cond_60092 = res_60089 == res_60077;
                    bool res_60093 = sle32(res_60090, res_60078);
                    bool res_60094 = sle32(res_60089, res_60077);
                    bool x_60095 = cond_60092 && res_60093;
                    bool x_60096 = !cond_60092;
                    bool y_60097 = res_60094 && x_60096;
                    bool res_60098 = x_60095 || y_60097;
                    bool cond_60099 = res_60098 == res_60082;
                    int32_t res_60100;
                    
                    if (cond_60099) {
                        res_60100 = res_60089;
                    } else {
                        res_60100 = res_60077;
                    }
                    
                    int32_t res_60101;
                    
                    if (cond_60099) {
                        res_60101 = res_60090;
                    } else {
                        res_60101 = res_60078;
                    }
                    
                    int32_t res_60102;
                    
                    if (cond_60099) {
                        res_60102 = res_60091;
                    } else {
                        res_60102 = res_60079;
                    }
                    res_60085 = res_60100;
                    res_60086 = res_60101;
                    res_60087 = res_60102;
                } else {
                    int32_t i_60103 = res_60072 ^ i_61674;
                    int32_t res_60104 = *(int32_t *) &xs_mem_63984.mem[i_60103 *
                                                                       4];
                    int32_t res_60105 = *(int32_t *) &xs_mem_63986.mem[i_60103 *
                                                                       4];
                    int32_t res_60106 = *(int32_t *) &xs_mem_63988.mem[i_60103 *
                                                                       4];
                    bool cond_60107 = res_60077 == res_60104;
                    bool res_60108 = sle32(res_60078, res_60105);
                    bool res_60109 = sle32(res_60077, res_60104);
                    bool x_60110 = cond_60107 && res_60108;
                    bool x_60111 = !cond_60107;
                    bool y_60112 = res_60109 && x_60111;
                    bool res_60113 = x_60110 || y_60112;
                    bool cond_60114 = res_60113 == res_60082;
                    int32_t res_60115;
                    
                    if (cond_60114) {
                        res_60115 = res_60104;
                    } else {
                        res_60115 = res_60077;
                    }
                    
                    int32_t res_60116;
                    
                    if (cond_60114) {
                        res_60116 = res_60105;
                    } else {
                        res_60116 = res_60078;
                    }
                    
                    int32_t res_60117;
                    
                    if (cond_60114) {
                        res_60117 = res_60106;
                    } else {
                        res_60117 = res_60079;
                    }
                    res_60085 = res_60115;
                    res_60086 = res_60116;
                    res_60087 = res_60117;
                }
                *(int32_t *) &mem_63991.mem[i_61674 * 4] = res_60085;
                *(int32_t *) &mem_63994.mem[i_61674 * 4] = res_60086;
                *(int32_t *) &mem_63997.mem[i_61674 * 4] = res_60087;
            }
            
            int64_t xs_mem_sizze_tmp_64573 = bytes_63989;
            struct memblock xs_mem_tmp_64574;
            
            xs_mem_tmp_64574.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64574, &mem_63991, "mem_63991") !=
                0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64575 = bytes_63989;
            struct memblock xs_mem_tmp_64576;
            
            xs_mem_tmp_64576.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64576, &mem_63994, "mem_63994") !=
                0)
                return 1;
            
            int64_t xs_mem_sizze_tmp_64577 = bytes_63989;
            struct memblock xs_mem_tmp_64578;
            
            xs_mem_tmp_64578.references = NULL;
            if (memblock_set(ctx, &xs_mem_tmp_64578, &mem_63997, "mem_63997") !=
                0)
                return 1;
            xs_mem_sizze_63983 = xs_mem_sizze_tmp_64573;
            if (memblock_set(ctx, &xs_mem_63984, &xs_mem_tmp_64574,
                             "xs_mem_tmp_64574") != 0)
                return 1;
            xs_mem_sizze_63985 = xs_mem_sizze_tmp_64575;
            if (memblock_set(ctx, &xs_mem_63986, &xs_mem_tmp_64576,
                             "xs_mem_tmp_64576") != 0)
                return 1;
            xs_mem_sizze_63987 = xs_mem_sizze_tmp_64577;
            if (memblock_set(ctx, &xs_mem_63988, &xs_mem_tmp_64578,
                             "xs_mem_tmp_64578") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64578, "xs_mem_tmp_64578") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64576, "xs_mem_tmp_64576") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_tmp_64574, "xs_mem_tmp_64574") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63997, "mem_63997") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63994, "mem_63994") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63991, "mem_63991") != 0)
                return 1;
        }
        res_mem_sizze_64010 = xs_mem_sizze_63983;
        if (memblock_set(ctx, &res_mem_64011, &xs_mem_63984, "xs_mem_63984") !=
            0)
            return 1;
        res_mem_sizze_64012 = xs_mem_sizze_63985;
        if (memblock_set(ctx, &res_mem_64013, &xs_mem_63986, "xs_mem_63986") !=
            0)
            return 1;
        res_mem_sizze_64014 = xs_mem_sizze_63987;
        if (memblock_set(ctx, &res_mem_64015, &xs_mem_63988, "xs_mem_63988") !=
            0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_64564 = res_mem_sizze_64010;
        struct memblock xs_mem_tmp_64565;
        
        xs_mem_tmp_64565.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_64565, &res_mem_64011,
                         "res_mem_64011") != 0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_64566 = res_mem_sizze_64012;
        struct memblock xs_mem_tmp_64567;
        
        xs_mem_tmp_64567.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_64567, &res_mem_64013,
                         "res_mem_64013") != 0)
            return 1;
        
        int64_t xs_mem_sizze_tmp_64568 = res_mem_sizze_64014;
        struct memblock xs_mem_tmp_64569;
        
        xs_mem_tmp_64569.references = NULL;
        if (memblock_set(ctx, &xs_mem_tmp_64569, &res_mem_64015,
                         "res_mem_64015") != 0)
            return 1;
        xs_mem_sizze_63977 = xs_mem_sizze_tmp_64564;
        if (memblock_set(ctx, &xs_mem_63978, &xs_mem_tmp_64565,
                         "xs_mem_tmp_64565") != 0)
            return 1;
        xs_mem_sizze_63979 = xs_mem_sizze_tmp_64566;
        if (memblock_set(ctx, &xs_mem_63980, &xs_mem_tmp_64567,
                         "xs_mem_tmp_64567") != 0)
            return 1;
        xs_mem_sizze_63981 = xs_mem_sizze_tmp_64568;
        if (memblock_set(ctx, &xs_mem_63982, &xs_mem_tmp_64569,
                         "xs_mem_tmp_64569") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_64569, "xs_mem_tmp_64569") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_64567, "xs_mem_tmp_64567") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_tmp_64565, "xs_mem_tmp_64565") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63988, "xs_mem_63988") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63986, "xs_mem_63986") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63984, "xs_mem_63984") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_64015, "res_mem_64015") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_64013, "res_mem_64013") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_64011, "res_mem_64011") != 0)
            return 1;
    }
    indexed_mem_sizze_64016 = xs_mem_sizze_63977;
    if (memblock_set(ctx, &indexed_mem_64017, &xs_mem_63978, "xs_mem_63978") !=
        0)
        return 1;
    indexed_mem_sizze_64018 = xs_mem_sizze_63979;
    if (memblock_set(ctx, &indexed_mem_64019, &xs_mem_63980, "xs_mem_63980") !=
        0)
        return 1;
    indexed_mem_sizze_64020 = xs_mem_sizze_63981;
    if (memblock_set(ctx, &indexed_mem_64021, &xs_mem_63982, "xs_mem_63982") !=
        0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
        return 1;
    
    bool i_p_m_t_s_leq_w_60118 = slt32(m_59922, sizze_59931);
    bool y_60119 = zzero_leq_i_p_m_t_s_59923 && i_p_m_t_s_leq_w_60118;
    bool ok_or_empty_60121 = empty_slice_59921 || y_60119;
    bool index_certs_60122;
    
    if (!ok_or_empty_60121) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:196:15-115 -> lib/github.com/diku-dk/sorts/merge_sort.fut:46:6-47:58",
                               "Index [", "", ":", last_offset_59906,
                               "] out of bounds for array of shape [",
                               sizze_59931, "].");
        if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    struct memblock mem_64024;
    
    mem_64024.references = NULL;
    if (memblock_alloc(ctx, &mem_64024, bytes_63902, "mem_64024"))
        return 1;
    
    struct memblock mem_64027;
    
    mem_64027.references = NULL;
    if (memblock_alloc(ctx, &mem_64027, bytes_63902, "mem_64027"))
        return 1;
    
    struct memblock mem_64029;
    
    mem_64029.references = NULL;
    if (memblock_alloc(ctx, &mem_64029, binop_x_63903, "mem_64029"))
        return 1;
    
    int32_t discard_61696;
    int32_t scanacc_61684 = -1;
    
    for (int32_t i_61689 = 0; i_61689 < last_offset_59906; i_61689++) {
        int32_t x_60149 = *(int32_t *) &indexed_mem_64017.mem[i_61689 * 4];
        int32_t i_p_o_62376 = -1 + i_61689;
        int32_t rot_i_62377 = smod32(i_p_o_62376, last_offset_59906);
        int32_t x_60150 = *(int32_t *) &indexed_mem_64017.mem[rot_i_62377 * 4];
        bool res_60152 = x_60149 == x_60150;
        bool res_60153 = !res_60152;
        int32_t res_60147;
        
        if (res_60153) {
            res_60147 = 0;
        } else {
            int32_t res_60148 = 1 + scanacc_61684;
            
            res_60147 = res_60148;
        }
        memmove(mem_64024.mem + i_61689 * 4, indexed_mem_64017.mem + i_61689 *
                4, sizeof(int32_t));
        *(int32_t *) &mem_64027.mem[i_61689 * 4] = res_60147;
        *(bool *) &mem_64029.mem[i_61689] = res_60153;
        
        int32_t scanacc_tmp_64585 = res_60147;
        
        scanacc_61684 = scanacc_tmp_64585;
    }
    discard_61696 = scanacc_61684;
    if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") != 0)
        return 1;
    
    struct memblock mem_64044;
    
    mem_64044.references = NULL;
    if (memblock_alloc(ctx, &mem_64044, bytes_63902, "mem_64044"))
        return 1;
    
    int32_t discard_61702;
    int32_t scanacc_61698 = 0;
    
    for (int32_t i_61700 = 0; i_61700 < last_offset_59906; i_61700++) {
        int32_t i_p_o_62382 = 1 + i_61700;
        int32_t rot_i_62383 = smod32(i_p_o_62382, last_offset_59906);
        bool x_60159 = *(bool *) &mem_64029.mem[rot_i_62383];
        int32_t res_60160 = btoi_bool_i32(x_60159);
        int32_t res_60158 = res_60160 + scanacc_61698;
        
        *(int32_t *) &mem_64044.mem[i_61700 * 4] = res_60158;
        
        int32_t scanacc_tmp_64589 = res_60158;
        
        scanacc_61698 = scanacc_tmp_64589;
    }
    discard_61702 = scanacc_61698;
    
    int32_t res_60161;
    
    if (loop_cond_59930) {
        bool index_certs_60162;
        
        if (!zzero_leq_i_p_m_t_s_59923) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:199:42-106 -> lib/github.com/diku-dk/segmented/segmented.fut:29:36-59",
                                   "Index [", m_59922,
                                   "] out of bounds for array of shape [",
                                   last_offset_59906, "].");
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t res_60163 = *(int32_t *) &mem_64044.mem[m_59922 * 4];
        
        res_60161 = res_60163;
    } else {
        res_60161 = 0;
    }
    
    bool bounds_invalid_upwards_60164 = slt32(res_60161, 0);
    bool eq_x_zz_60165 = 0 == res_60161;
    bool not_p_60166 = !bounds_invalid_upwards_60164;
    bool p_and_eq_x_y_60167 = eq_x_zz_60165 && not_p_60166;
    bool dim_zzero_60168 = bounds_invalid_upwards_60164 || p_and_eq_x_y_60167;
    bool both_empty_60169 = eq_x_zz_60165 && dim_zzero_60168;
    bool empty_or_match_60173 = not_p_60166 || both_empty_60169;
    bool empty_or_match_cert_60174;
    
    if (!empty_or_match_60173) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                               "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:199:42-106 -> lib/github.com/diku-dk/segmented/segmented.fut:33:17-41 -> /futlib/array.fut:66:1-67:19",
                               "Function return value does not match shape of type ",
                               "*", "[", res_60161, "]", "t");
        if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
            return 1;
        if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
            return 1;
        if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
            return 1;
        if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
            return 1;
        if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
            return 1;
        return 1;
    }
    
    int64_t binop_x_64050 = sext_i32_i64(res_60161);
    int64_t bytes_64049 = 4 * binop_x_64050;
    struct memblock mem_64051;
    
    mem_64051.references = NULL;
    if (memblock_alloc(ctx, &mem_64051, bytes_64049, "mem_64051"))
        return 1;
    for (int32_t i_64591 = 0; i_64591 < res_60161; i_64591++) {
        *(int32_t *) &mem_64051.mem[i_64591 * 4] = 0;
    }
    
    struct memblock mem_64054;
    
    mem_64054.references = NULL;
    if (memblock_alloc(ctx, &mem_64054, bytes_64049, "mem_64054"))
        return 1;
    for (int32_t i_64592 = 0; i_64592 < res_60161; i_64592++) {
        *(int32_t *) &mem_64054.mem[i_64592 * 4] = -1;
    }
    for (int32_t write_iter_61703 = 0; write_iter_61703 < last_offset_59906;
         write_iter_61703++) {
        int32_t write_iv_61706 = *(int32_t *) &mem_64044.mem[write_iter_61703 *
                                                             4];
        int32_t i_p_o_62386 = 1 + write_iter_61703;
        int32_t rot_i_62387 = smod32(i_p_o_62386, last_offset_59906);
        bool write_iv_61707 = *(bool *) &mem_64029.mem[rot_i_62387];
        int32_t res_60183;
        
        if (write_iv_61707) {
            int32_t res_60184 = write_iv_61706 - 1;
            
            res_60183 = res_60184;
        } else {
            res_60183 = -1;
        }
        
        bool less_than_zzero_61710 = slt32(res_60183, 0);
        bool greater_than_sizze_61711 = sle32(res_60161, res_60183);
        bool outside_bounds_dim_61712 = less_than_zzero_61710 ||
             greater_than_sizze_61711;
        
        if (!outside_bounds_dim_61712) {
            memmove(mem_64051.mem + res_60183 * 4, mem_64024.mem +
                    write_iter_61703 * 4, sizeof(int32_t));
        }
        if (!outside_bounds_dim_61712) {
            memmove(mem_64054.mem + res_60183 * 4, mem_64027.mem +
                    write_iter_61703 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
        return 1;
    
    struct memblock mem_64065;
    
    mem_64065.references = NULL;
    if (memblock_alloc(ctx, &mem_64065, 12, "mem_64065"))
        return 1;
    for (int32_t i_64595 = 0; i_64595 < 3; i_64595++) {
        *(int32_t *) &mem_64065.mem[i_64595 * 4] = 0;
    }
    for (int32_t write_iter_61722 = 0; write_iter_61722 < res_60161;
         write_iter_61722++) {
        int32_t write_iv_61724 = *(int32_t *) &mem_64051.mem[write_iter_61722 *
                                                             4];
        bool less_than_zzero_61726 = slt32(write_iv_61724, 0);
        bool greater_than_sizze_61727 = sle32(3, write_iv_61724);
        bool outside_bounds_dim_61728 = less_than_zzero_61726 ||
             greater_than_sizze_61727;
        
        if (!outside_bounds_dim_61728) {
            memmove(mem_64065.mem + write_iv_61724 * 4, mem_64054.mem +
                    write_iter_61722 * 4, sizeof(int32_t));
        }
    }
    if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
        return 1;
    
    struct memblock mem_64072;
    
    mem_64072.references = NULL;
    if (memblock_alloc(ctx, &mem_64072, 16, "mem_64072"))
        return 1;
    
    struct memblock mem_64075;
    
    mem_64075.references = NULL;
    if (memblock_alloc(ctx, &mem_64075, 16, "mem_64075"))
        return 1;
    
    int32_t discard_61802;
    int32_t scanacc_61795 = 0;
    
    for (int32_t i_61799 = 0; i_61799 < 4; i_61799++) {
        bool index_concat_cmp_62407 = sle32(1, i_61799);
        int32_t index_concat_branch_62411;
        
        if (index_concat_cmp_62407) {
            int32_t index_concat_i_62408 = i_61799 - 1;
            int32_t index_concat_62409 =
                    *(int32_t *) &mem_64065.mem[index_concat_i_62408 * 4];
            
            index_concat_branch_62411 = index_concat_62409;
        } else {
            index_concat_branch_62411 = 0;
        }
        
        int32_t res_60251 = scanacc_61795 + index_concat_branch_62411;
        
        *(int32_t *) &mem_64072.mem[i_61799 * 4] = res_60251;
        *(int32_t *) &mem_64075.mem[i_61799 * 4] = 0;
        
        int32_t scanacc_tmp_64597 = res_60251;
        
        scanacc_61795 = scanacc_tmp_64597;
    }
    discard_61802 = scanacc_61795;
    if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
        return 1;
    
    struct memblock mem_64086;
    
    mem_64086.references = NULL;
    if (memblock_alloc(ctx, &mem_64086, 12, "mem_64086"))
        return 1;
    
    int32_t discard_61809;
    int32_t scanacc_61805 = 0;
    
    for (int32_t i_61807 = 0; i_61807 < 3; i_61807++) {
        int32_t res_60259 = 2 + scanacc_61805;
        
        *(int32_t *) &mem_64086.mem[i_61807 * 4] = res_60259;
        
        int32_t scanacc_tmp_64600 = res_60259;
        
        scanacc_61805 = scanacc_tmp_64600;
    }
    discard_61809 = scanacc_61805;
    
    struct memblock mem_64093;
    
    mem_64093.references = NULL;
    if (memblock_alloc(ctx, &mem_64093, 24, "mem_64093"))
        return 1;
    for (int32_t i_64602 = 0; i_64602 < 6; i_64602++) {
        *(int32_t *) &mem_64093.mem[i_64602 * 4] = 0;
    }
    for (int32_t write_iter_61810 = 0; write_iter_61810 < 3;
         write_iter_61810++) {
        bool cond_60264 = write_iter_61810 == 0;
        int32_t res_60265;
        
        if (cond_60264) {
            res_60265 = 0;
        } else {
            int32_t i_60266 = write_iter_61810 - 1;
            int32_t res_60267 = *(int32_t *) &mem_64086.mem[i_60266 * 4];
            
            res_60265 = res_60267;
        }
        
        bool less_than_zzero_61813 = slt32(res_60265, 0);
        bool greater_than_sizze_61814 = sle32(6, res_60265);
        bool outside_bounds_dim_61815 = less_than_zzero_61813 ||
             greater_than_sizze_61814;
        
        if (!outside_bounds_dim_61815) {
            struct memblock mem_64098;
            
            mem_64098.references = NULL;
            if (memblock_alloc(ctx, &mem_64098, 4, "mem_64098"))
                return 1;
            
            int32_t x_64605;
            
            for (int32_t i_64604 = 0; i_64604 < 1; i_64604++) {
                x_64605 = write_iter_61810 + sext_i32_i32(i_64604);
                *(int32_t *) &mem_64098.mem[i_64604 * 4] = x_64605;
            }
            memmove(mem_64093.mem + res_60265 * 4, mem_64098.mem + 0,
                    sizeof(int32_t));
            if (memblock_unref(ctx, &mem_64098, "mem_64098") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64098, "mem_64098") != 0)
                return 1;
        }
    }
    if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
        return 1;
    
    struct memblock mem_64103;
    
    mem_64103.references = NULL;
    if (memblock_alloc(ctx, &mem_64103, 24, "mem_64103"))
        return 1;
    
    int32_t discard_61829;
    int32_t scanacc_61822 = 0;
    
    for (int32_t i_61825 = 0; i_61825 < 6; i_61825++) {
        int32_t x_60277 = *(int32_t *) &mem_64093.mem[i_61825 * 4];
        bool res_60278 = slt32(0, x_60277);
        int32_t res_60275;
        
        if (res_60278) {
            res_60275 = x_60277;
        } else {
            int32_t res_60276 = x_60277 + scanacc_61822;
            
            res_60275 = res_60276;
        }
        *(int32_t *) &mem_64103.mem[i_61825 * 4] = res_60275;
        
        int32_t scanacc_tmp_64606 = res_60275;
        
        scanacc_61822 = scanacc_tmp_64606;
    }
    discard_61829 = scanacc_61822;
    if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
        return 1;
    
    struct memblock mem_64110;
    
    mem_64110.references = NULL;
    if (memblock_alloc(ctx, &mem_64110, 24, "mem_64110"))
        return 1;
    
    int32_t discard_61840;
    int32_t scanacc_61833 = 0;
    
    for (int32_t i_61836 = 0; i_61836 < 6; i_61836++) {
        int32_t x_60342 = *(int32_t *) &mem_64103.mem[i_61836 * 4];
        int32_t i_p_o_62417 = -1 + i_61836;
        int32_t rot_i_62418 = smod32(i_p_o_62417, 6);
        int32_t x_60343 = *(int32_t *) &mem_64103.mem[rot_i_62418 * 4];
        bool res_60345 = x_60342 == x_60343;
        bool res_60346 = !res_60345;
        int32_t res_60340;
        
        if (res_60346) {
            res_60340 = 1;
        } else {
            int32_t res_60341 = 1 + scanacc_61833;
            
            res_60340 = res_60341;
        }
        *(int32_t *) &mem_64110.mem[i_61836 * 4] = res_60340;
        
        int32_t scanacc_tmp_64608 = res_60340;
        
        scanacc_61833 = scanacc_tmp_64608;
    }
    discard_61840 = scanacc_61833;
    
    struct memblock mem_64117;
    
    mem_64117.references = NULL;
    if (memblock_alloc(ctx, &mem_64117, 24, "mem_64117"))
        return 1;
    
    int32_t discard_61859;
    int32_t scanacc_61849 = 0;
    
    for (int32_t i_61854 = 0; i_61854 < 6; i_61854++) {
        int32_t x_60370 = *(int32_t *) &mem_64110.mem[i_61854 * 4];
        int32_t x_60371 = *(int32_t *) &mem_64103.mem[i_61854 * 4];
        int32_t res_60374 = x_60370 - 1;
        bool x_60375 = sle32(0, x_60371);
        bool y_60376 = slt32(x_60371, 4);
        bool bounds_check_60377 = x_60375 && y_60376;
        bool index_certs_60378;
        
        if (!bounds_check_60377) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:186:21-29",
                                   "Index [", x_60371,
                                   "] out of bounds for array of shape [", 4,
                                   "].");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t i_60379 = *(int32_t *) &mem_64072.mem[x_60371 * 4];
        int32_t i_60380 = 1 + x_60371;
        bool x_60381 = sle32(0, i_60380);
        bool y_60382 = slt32(i_60380, 4);
        bool bounds_check_60383 = x_60381 && y_60382;
        bool index_certs_60384;
        
        if (!bounds_check_60383) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:186:31-41",
                                   "Index [", i_60380,
                                   "] out of bounds for array of shape [", 4,
                                   "].");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t j_60385 = *(int32_t *) &mem_64072.mem[i_60380 * 4];
        int32_t j_m_i_60386 = j_60385 - i_60379;
        bool empty_slice_60387 = j_m_i_60386 == 0;
        int32_t m_60388 = j_m_i_60386 - 1;
        int32_t i_p_m_t_s_60389 = i_60379 + m_60388;
        bool zzero_leq_i_p_m_t_s_60390 = sle32(0, i_p_m_t_s_60389);
        bool i_p_m_t_s_leq_w_60391 = slt32(i_p_m_t_s_60389, last_offset_59906);
        bool zzero_lte_i_60392 = sle32(0, i_60379);
        bool i_lte_j_60393 = sle32(i_60379, j_60385);
        bool y_60394 = i_p_m_t_s_leq_w_60391 && zzero_lte_i_60392;
        bool y_60395 = zzero_leq_i_p_m_t_s_60390 && y_60394;
        bool y_60396 = i_lte_j_60393 && y_60395;
        bool forwards_ok_60397 = zzero_lte_i_60392 && y_60396;
        bool ok_or_empty_60398 = empty_slice_60387 || forwards_ok_60397;
        bool index_certs_60399;
        
        if (!ok_or_empty_60398) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:186:15-42",
                                   "Index [", i_60379, ":", j_60385,
                                   "] out of bounds for array of shape [",
                                   last_offset_59906, "].");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        bool x_60402 = sle32(0, res_60374);
        bool y_60403 = slt32(res_60374, 4);
        bool bounds_check_60404 = x_60402 && y_60403;
        bool index_certs_60405;
        
        if (!bounds_check_60404) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:187:21-29",
                                   "Index [", res_60374,
                                   "] out of bounds for array of shape [", 4,
                                   "].");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t i_60406 = *(int32_t *) &mem_64075.mem[res_60374 * 4];
        int32_t i_60407 = 1 + res_60374;
        bool x_60408 = sle32(0, i_60407);
        bool y_60409 = slt32(i_60407, 4);
        bool bounds_check_60410 = x_60408 && y_60409;
        bool index_certs_60411;
        
        if (!bounds_check_60410) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:187:31-41",
                                   "Index [", i_60407,
                                   "] out of bounds for array of shape [", 4,
                                   "].");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t j_60412 = *(int32_t *) &mem_64075.mem[i_60407 * 4];
        int32_t j_m_i_60413 = j_60412 - i_60406;
        bool empty_slice_60414 = j_m_i_60413 == 0;
        int32_t m_60415 = j_m_i_60413 - 1;
        int32_t i_p_m_t_s_60416 = i_60406 + m_60415;
        bool zzero_leq_i_p_m_t_s_60417 = sle32(0, i_p_m_t_s_60416);
        bool i_p_m_t_s_leq_w_60418 = slt32(i_p_m_t_s_60416, 0);
        bool zzero_lte_i_60419 = sle32(0, i_60406);
        bool i_lte_j_60420 = sle32(i_60406, j_60412);
        bool y_60421 = i_p_m_t_s_leq_w_60418 && zzero_lte_i_60419;
        bool y_60422 = zzero_leq_i_p_m_t_s_60417 && y_60421;
        bool y_60423 = i_lte_j_60420 && y_60422;
        bool forwards_ok_60424 = zzero_lte_i_60419 && y_60423;
        bool ok_or_empty_60425 = empty_slice_60414 || forwards_ok_60424;
        bool index_certs_60426;
        
        if (!ok_or_empty_60425) {
            ctx->error = msgprintf("Error at %s:\n%s%d%s%d%s%d%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:187:15-42",
                                   "Index [", i_60406, ":", j_60412,
                                   "] out of bounds for array of shape [", 0,
                                   "].");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        bool bounds_invalid_upwards_60428 = slt32(j_m_i_60413, 0);
        bool eq_x_zz_60429 = 0 == j_m_i_60413;
        bool not_p_60430 = !bounds_invalid_upwards_60428;
        bool p_and_eq_x_y_60431 = eq_x_zz_60429 && not_p_60430;
        bool dim_zzero_60432 = bounds_invalid_upwards_60428 ||
             p_and_eq_x_y_60431;
        bool both_empty_60433 = eq_x_zz_60429 && dim_zzero_60432;
        bool empty_or_match_60436 = not_p_60430 || both_empty_60433;
        bool empty_or_match_cert_60437;
        
        if (!empty_or_match_60436) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%s\n",
                                   "tupleTest.fut:173:1-181:55 -> tupleTest.fut:180:14-19 -> tupleTest.fut:166:13-27 -> tupleSparse.fut:213:3-34 -> tupleSparse.fut:208:15-79 -> lib/github.com/diku-dk/segmented/segmented.fut:73:6-52 -> /futlib/soacs.fut:51:3-37 -> /futlib/soacs.fut:51:19-23 -> lib/github.com/diku-dk/segmented/segmented.fut:73:20-40 -> tupleSparse.fut:189:13-192:23 -> tupleSparse.fut:189:48-71 -> tupleSparse.fut:144:55-60 -> /futlib/array.fut:61:1-62:12",
                                   "Function return value does not match shape of type ",
                                   "*", "[", j_m_i_60413, "]",
                                   "intrinsics.i32");
            if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
                return 1;
            if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") !=
                0)
                return 1;
            if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") !=
                0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
                return 1;
            if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
                return 1;
            if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
                return 1;
            return 1;
        }
        
        int32_t res_60439;
        int32_t redout_61843 = 0;
        
        for (int32_t i_61844 = 0; i_61844 < j_m_i_60386; i_61844++) {
            int32_t j_p_i_t_s_62425 = i_60379 + i_61844;
            int32_t x_60443 =
                    *(int32_t *) &indexed_mem_64019.mem[j_p_i_t_s_62425 * 4];
            int32_t x_60444 =
                    *(int32_t *) &indexed_mem_64021.mem[j_p_i_t_s_62425 * 4];
            int32_t res_60445;
            int32_t redout_61841 = j_m_i_60413;
            
            for (int32_t i_61842 = 0; i_61842 < j_m_i_60413; i_61842++) {
                int32_t j_p_i_t_s_62421 = i_60406 + i_61842;
                int32_t x_60449 = *(int32_t *) &mem_62605.mem[j_p_i_t_s_62421 *
                                                              4];
                int32_t x_60450 = i_61842;
                bool cond_60451 = x_60449 == x_60443;
                int32_t res_60452;
                
                if (cond_60451) {
                    res_60452 = x_60450;
                } else {
                    res_60452 = j_m_i_60413;
                }
                
                int32_t res_60448 = smin32(res_60452, redout_61841);
                int32_t redout_tmp_64613 = res_60448;
                
                redout_61841 = redout_tmp_64613;
            }
            res_60445 = redout_61841;
            
            bool cond_60453 = res_60445 == j_m_i_60413;
            int32_t res_60454;
            
            if (cond_60453) {
                res_60454 = -1;
            } else {
                res_60454 = res_60445;
            }
            
            bool eq_x_zz_60455 = -1 == res_60445;
            bool not_p_60456 = !cond_60453;
            bool p_and_eq_x_y_60457 = eq_x_zz_60455 && not_p_60456;
            bool cond_60458 = cond_60453 || p_and_eq_x_y_60457;
            int32_t res_60459;
            
            if (cond_60458) {
                res_60459 = 0;
            } else {
                int32_t j_p_i_t_s_60460 = i_60406 + res_60454;
                int32_t res_60461 =
                        *(int32_t *) &mem_62605.mem[j_p_i_t_s_60460 * 4];
                int32_t res_60462 = x_60444 * res_60461;
                
                res_60459 = res_60462;
            }
            
            int32_t res_60442 = res_60459 + redout_61843;
            int32_t redout_tmp_64612 = res_60442;
            
            redout_61843 = redout_tmp_64612;
        }
        res_60439 = redout_61843;
        
        bool not_arg_60463 = res_60439 == 0;
        bool res_60464 = !not_arg_60463;
        int32_t part_res_60466;
        
        if (res_60464) {
            part_res_60466 = 1;
        } else {
            part_res_60466 = 0;
        }
        
        int32_t zz_60368 = part_res_60466 + scanacc_61849;
        
        *(int32_t *) &mem_64117.mem[i_61854 * 4] = zz_60368;
        
        int32_t scanacc_tmp_64610 = zz_60368;
        
        scanacc_61849 = scanacc_tmp_64610;
    }
    discard_61859 = scanacc_61849;
    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
        return 1;
    
    struct memblock mem_64124;
    
    mem_64124.references = NULL;
    if (memblock_alloc(ctx, &mem_64124, 24, "mem_64124"))
        return 1;
    for (int32_t i_61862 = 0; i_61862 < 6; i_61862++) {
        memmove(mem_64124.mem + i_61862 * 4, mem_64117.mem + i_61862 * 4,
                sizeof(int32_t));
    }
    if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
        return 1;
    
    int32_t last_offset_60486 = *(int32_t *) &mem_64124.mem[20];
    
    if (memblock_unref(ctx, &mem_64124, "mem_64124") != 0)
        return 1;
    
    bool dim_eq_60506 = 0 == last_offset_60486;
    bool x_60516 = cond_57903 && res_58206;
    bool x_60517 = res_58365 && x_60516;
    bool x_60518 = res_58434 && x_60517;
    bool x_60519 = res_59501 && x_60518;
    bool x_60520 = dim_eq_60506 && x_60519;
    
    scalar_out_64129 = x_60520;
    *out_scalar_out_64615 = scalar_out_64129;
    if (memblock_unref(ctx, &mem_64124, "mem_64124") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64117, "mem_64117") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64110, "mem_64110") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64103, "mem_64103") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64093, "mem_64093") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64086, "mem_64086") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64075, "mem_64075") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64072, "mem_64072") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64065, "mem_64065") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64054, "mem_64054") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64051, "mem_64051") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64044, "mem_64044") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64029, "mem_64029") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64027, "mem_64027") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_64024, "mem_64024") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_63982, "xs_mem_63982") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_63980, "xs_mem_63980") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_63978, "xs_mem_63978") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_64021, "indexed_mem_64021") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_64019, "indexed_mem_64019") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_64017, "indexed_mem_64017") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63976, "res_mem_63976") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63974, "res_mem_63974") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63972, "res_mem_63972") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63910, "mem_63910") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63907, "mem_63907") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63904, "mem_63904") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63893, "mem_63893") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63890, "mem_63890") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63885, "mem_63885") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63880, "mem_63880") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63211, "mem_63211") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63200, "mem_63200") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63197, "mem_63197") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63190, "mem_63190") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63183, "mem_63183") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63172, "mem_63172") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63170, "mem_63170") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_63128, "xs_mem_63128") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_63126, "xs_mem_63126") != 0)
        return 1;
    if (memblock_unref(ctx, &xs_mem_63124, "xs_mem_63124") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_63167, "indexed_mem_63167") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_63165, "indexed_mem_63165") != 0)
        return 1;
    if (memblock_unref(ctx, &indexed_mem_63163, "indexed_mem_63163") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63122, "res_mem_63122") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63120, "res_mem_63120") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_63118, "res_mem_63118") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63074, "mem_63074") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63071, "mem_63071") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63068, "mem_63068") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63047, "mem_63047") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63044, "mem_63044") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63041, "mem_63041") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63030, "mem_63030") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63027, "mem_63027") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63022, "mem_63022") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_63017, "mem_63017") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62986, "mem_62986") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62968, "mem_62968") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62965, "mem_62965") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62962, "mem_62962") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62955, "mem_62955") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62950, "mem_62950") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62945, "mem_62945") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62866, "mem_62866") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62845, "mem_62845") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62842, "mem_62842") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62839, "mem_62839") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62828, "mem_62828") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62825, "mem_62825") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62820, "mem_62820") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62815, "mem_62815") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62692, "mem_62692") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62689, "mem_62689") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62686, "mem_62686") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_62683, "res_mem_62683") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_62681, "res_mem_62681") != 0)
        return 1;
    if (memblock_unref(ctx, &res_mem_62679, "res_mem_62679") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62665, "mem_62665") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62662, "mem_62662") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62659, "mem_62659") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62638, "mem_62638") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62635, "mem_62635") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62632, "mem_62632") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62621, "mem_62621") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62618, "mem_62618") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62613, "mem_62613") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62608, "mem_62608") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62605, "mem_62605") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62493, "mem_62493") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62490, "mem_62490") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62487, "mem_62487") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62484, "mem_62484") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62481, "mem_62481") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62463, "mem_62463") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62460, "mem_62460") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62457, "mem_62457") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62446, "mem_62446") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62443, "mem_62443") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62438, "mem_62438") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62433, "mem_62433") != 0)
        return 1;
    if (memblock_unref(ctx, &mem_62430, "mem_62430") != 0)
        return 1;
    return 0;
}
int futhark_entry_main(struct futhark_context *ctx, bool *out0)
{
    bool scalar_out_64129;
    
    lock_lock(&ctx->lock);
    
    int ret = futrts_main(ctx, &scalar_out_64129);
    
    if (ret == 0) {
        *out0 = scalar_out_64129;
    }
    lock_unlock(&ctx->lock);
    return ret;
}
