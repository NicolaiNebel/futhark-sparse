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
    bool result_14249;
    
    if (perform_warmup) {
        time_runs = 0;
        
        int r;
        
        assert(futhark_context_sync(ctx) == 0);
        t_start = get_wall_time();
        r = futhark_entry_main(ctx, &result_14249);
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
        r = futhark_entry_main(ctx, &result_14249);
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
    write_scalar(stdout, binary_output, &bool_info, &result_14249);
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

static int32_t static_array_realtype_14240[4] = {1, 2, 3, 4};
static int32_t static_array_realtype_14241[4] = {0, 0, 1, 1};
static int32_t static_array_realtype_14242[4] = {0, 1, 0, 1};
static int32_t static_array_realtype_14243[3] = {0, 1, 1};
static int32_t static_array_realtype_14244[3] = {1, 0, 1};
static int32_t static_array_realtype_14245[3] = {1, 2, 3};
static int32_t static_array_realtype_14246[3] = {1, 4, 3};
static int32_t static_array_realtype_14247[4] = {0, 1, 1, 0};
static int32_t static_array_realtype_14248[4] = {1, 0, 1, 0};
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
    struct memblock static_array_14145;
    struct memblock static_array_14153;
    struct memblock static_array_14154;
    struct memblock static_array_14158;
    struct memblock static_array_14159;
    struct memblock static_array_14160;
    struct memblock static_array_14183;
    struct memblock static_array_14202;
    struct memblock static_array_14203;
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
    ctx->static_array_14145 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14240,
                                                 0};
    ctx->static_array_14153 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14241,
                                                 0};
    ctx->static_array_14154 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14242,
                                                 0};
    ctx->static_array_14158 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14243,
                                                 0};
    ctx->static_array_14159 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14244,
                                                 0};
    ctx->static_array_14160 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14245,
                                                 0};
    ctx->static_array_14183 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14246,
                                                 0};
    ctx->static_array_14202 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14247,
                                                 0};
    ctx->static_array_14203 = (struct memblock) {NULL,
                                                 (char *) static_array_realtype_14248,
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
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_14239);
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
static int futrts_main(struct futhark_context *ctx, bool *out_scalar_out_14239)
{
    bool scalar_out_14144;
    struct memblock mem_13689;
    
    mem_13689.references = NULL;
    memblock_alloc(ctx, &mem_13689, 16, "mem_13689");
    
    struct memblock static_array_14145 = ctx->static_array_14145;
    
    memmove(mem_13689.mem + 0, static_array_14145.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_13692;
    
    mem_13692.references = NULL;
    memblock_alloc(ctx, &mem_13692, 16, "mem_13692");
    
    struct memblock mem_13697;
    
    mem_13697.references = NULL;
    memblock_alloc(ctx, &mem_13697, 8, "mem_13697");
    for (int32_t i_13218 = 0; i_13218 < 2; i_13218++) {
        for (int32_t i_14147 = 0; i_14147 < 2; i_14147++) {
            *(int32_t *) &mem_13697.mem[i_14147 * 4] = i_13218;
        }
        memmove(mem_13692.mem + 2 * i_13218 * 4, mem_13697.mem + 0, 2 *
                sizeof(int32_t));
    }
    memblock_unref(ctx, &mem_13697, "mem_13697");
    
    struct memblock mem_13702;
    
    mem_13702.references = NULL;
    memblock_alloc(ctx, &mem_13702, 16, "mem_13702");
    
    int32_t discard_13225;
    int32_t scanacc_13221 = 0;
    
    for (int32_t i_13223 = 0; i_13223 < 4; i_13223++) {
        int32_t zz_12645 = 1 + scanacc_13221;
        
        *(int32_t *) &mem_13702.mem[i_13223 * 4] = zz_12645;
        
        int32_t scanacc_tmp_14148 = zz_12645;
        
        scanacc_13221 = scanacc_tmp_14148;
    }
    discard_13225 = scanacc_13221;
    
    int32_t last_offset_12647 = *(int32_t *) &mem_13702.mem[12];
    int64_t binop_x_13708 = sext_i32_i64(last_offset_12647);
    int64_t bytes_13707 = 4 * binop_x_13708;
    struct memblock mem_13709;
    
    mem_13709.references = NULL;
    memblock_alloc(ctx, &mem_13709, bytes_13707, "mem_13709");
    
    struct memblock mem_13712;
    
    mem_13712.references = NULL;
    memblock_alloc(ctx, &mem_13712, bytes_13707, "mem_13712");
    
    struct memblock mem_13715;
    
    mem_13715.references = NULL;
    memblock_alloc(ctx, &mem_13715, bytes_13707, "mem_13715");
    for (int32_t write_iter_13226 = 0; write_iter_13226 < 4;
         write_iter_13226++) {
        int32_t write_iv_13230 = *(int32_t *) &mem_13702.mem[write_iter_13226 *
                                                             4];
        int32_t new_index_13567 = squot32(write_iter_13226, 2);
        int32_t binop_y_13569 = 2 * new_index_13567;
        int32_t new_index_13570 = write_iter_13226 - binop_y_13569;
        int32_t write_iv_13231 = *(int32_t *) &mem_13692.mem[(new_index_13567 *
                                                              2 +
                                                              new_index_13570) *
                                                             4];
        int32_t write_iv_13233 = *(int32_t *) &mem_13689.mem[write_iter_13226 *
                                                             4];
        int32_t this_offset_12658 = -1 + write_iv_13230;
        bool less_than_zzero_13234 = slt32(this_offset_12658, 0);
        bool greater_than_sizze_13235 = sle32(last_offset_12647,
                                              this_offset_12658);
        bool outside_bounds_dim_13236 = less_than_zzero_13234 ||
             greater_than_sizze_13235;
        
        if (!outside_bounds_dim_13236) {
            *(int32_t *) &mem_13709.mem[this_offset_12658 * 4] = write_iv_13231;
        }
        if (!outside_bounds_dim_13236) {
            *(int32_t *) &mem_13712.mem[this_offset_12658 * 4] =
                new_index_13570;
        }
        if (!outside_bounds_dim_13236) {
            *(int32_t *) &mem_13715.mem[this_offset_12658 * 4] = write_iv_13233;
        }
    }
    memblock_unref(ctx, &mem_13692, "mem_13692");
    memblock_unref(ctx, &mem_13702, "mem_13702");
    
    int32_t x_12659 = abs(last_offset_12647);
    bool empty_slice_12660 = x_12659 == 0;
    int32_t m_12661 = x_12659 - 1;
    bool zzero_leq_i_p_m_t_s_12662 = sle32(0, m_12661);
    bool i_p_m_t_s_leq_w_12663 = slt32(m_12661, last_offset_12647);
    bool y_12664 = zzero_leq_i_p_m_t_s_12662 && i_p_m_t_s_leq_w_12663;
    bool ok_or_empty_12665 = empty_slice_12660 || y_12664;
    bool index_certs_12666;
    
    if (!ok_or_empty_12665) {
        ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                               "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:11-76:15 -> tupleTest.fut:15:13-15:29 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                               "Index [", "", ":", last_offset_12647,
                               "] out of bounds for array of shape [",
                               last_offset_12647, "].");
        memblock_unref(ctx, &mem_13715, "mem_13715");
        memblock_unref(ctx, &mem_13712, "mem_13712");
        memblock_unref(ctx, &mem_13709, "mem_13709");
        memblock_unref(ctx, &mem_13702, "mem_13702");
        memblock_unref(ctx, &mem_13697, "mem_13697");
        memblock_unref(ctx, &mem_13692, "mem_13692");
        memblock_unref(ctx, &mem_13689, "mem_13689");
        return 1;
    }
    
    struct memblock mem_13736;
    
    mem_13736.references = NULL;
    memblock_alloc(ctx, &mem_13736, 16, "mem_13736");
    
    struct memblock static_array_14153 = ctx->static_array_14153;
    
    memmove(mem_13736.mem + 0, static_array_14153.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_13739;
    
    mem_13739.references = NULL;
    memblock_alloc(ctx, &mem_13739, 16, "mem_13739");
    
    struct memblock static_array_14154 = ctx->static_array_14154;
    
    memmove(mem_13739.mem + 0, static_array_14154.mem + 0, 4 * sizeof(int32_t));
    
    bool dim_eq_12672 = x_12659 == 4;
    bool arrays_equal_12673;
    
    if (dim_eq_12672) {
        bool all_equal_12675;
        bool redout_13252 = 1;
        
        for (int32_t i_13253 = 0; i_13253 < x_12659; i_13253++) {
            int32_t x_12679 = *(int32_t *) &mem_13709.mem[i_13253 * 4];
            int32_t y_12680 = *(int32_t *) &mem_13736.mem[i_13253 * 4];
            bool res_12681 = x_12679 == y_12680;
            bool res_12678 = res_12681 && redout_13252;
            bool redout_tmp_14155 = res_12678;
            
            redout_13252 = redout_tmp_14155;
        }
        all_equal_12675 = redout_13252;
        arrays_equal_12673 = all_equal_12675;
    } else {
        arrays_equal_12673 = 0;
    }
    memblock_unref(ctx, &mem_13709, "mem_13709");
    memblock_unref(ctx, &mem_13736, "mem_13736");
    
    bool arrays_equal_12682;
    
    if (dim_eq_12672) {
        bool all_equal_12684;
        bool redout_13254 = 1;
        
        for (int32_t i_13255 = 0; i_13255 < x_12659; i_13255++) {
            int32_t x_12688 = *(int32_t *) &mem_13712.mem[i_13255 * 4];
            int32_t y_12689 = *(int32_t *) &mem_13739.mem[i_13255 * 4];
            bool res_12690 = x_12688 == y_12689;
            bool res_12687 = res_12690 && redout_13254;
            bool redout_tmp_14156 = res_12687;
            
            redout_13254 = redout_tmp_14156;
        }
        all_equal_12684 = redout_13254;
        arrays_equal_12682 = all_equal_12684;
    } else {
        arrays_equal_12682 = 0;
    }
    memblock_unref(ctx, &mem_13712, "mem_13712");
    memblock_unref(ctx, &mem_13739, "mem_13739");
    
    bool eq_12691 = arrays_equal_12673 && arrays_equal_12682;
    bool res_12692;
    
    if (eq_12691) {
        bool arrays_equal_12693;
        
        if (dim_eq_12672) {
            bool all_equal_12695;
            bool redout_13256 = 1;
            
            for (int32_t i_13257 = 0; i_13257 < x_12659; i_13257++) {
                int32_t x_12699 = *(int32_t *) &mem_13715.mem[i_13257 * 4];
                int32_t y_12700 = *(int32_t *) &mem_13689.mem[i_13257 * 4];
                bool res_12701 = x_12699 == y_12700;
                bool res_12698 = res_12701 && redout_13256;
                bool redout_tmp_14157 = res_12698;
                
                redout_13256 = redout_tmp_14157;
            }
            all_equal_12695 = redout_13256;
            arrays_equal_12693 = all_equal_12695;
        } else {
            arrays_equal_12693 = 0;
        }
        res_12692 = arrays_equal_12693;
    } else {
        res_12692 = 0;
    }
    memblock_unref(ctx, &mem_13715, "mem_13715");
    
    struct memblock mem_13742;
    
    mem_13742.references = NULL;
    memblock_alloc(ctx, &mem_13742, 12, "mem_13742");
    
    struct memblock static_array_14158 = ctx->static_array_14158;
    
    memmove(mem_13742.mem + 0, static_array_14158.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_13745;
    
    mem_13745.references = NULL;
    memblock_alloc(ctx, &mem_13745, 12, "mem_13745");
    
    struct memblock static_array_14159 = ctx->static_array_14159;
    
    memmove(mem_13745.mem + 0, static_array_14159.mem + 0, 3 * sizeof(int32_t));
    
    struct memblock mem_13748;
    
    mem_13748.references = NULL;
    memblock_alloc(ctx, &mem_13748, 12, "mem_13748");
    
    struct memblock static_array_14160 = ctx->static_array_14160;
    
    memmove(mem_13748.mem + 0, static_array_14160.mem + 0, 3 * sizeof(int32_t));
    
    bool cond_12706;
    
    if (res_12692) {
        struct memblock mem_13751;
        
        mem_13751.references = NULL;
        memblock_alloc(ctx, &mem_13751, 16, "mem_13751");
        
        struct memblock mem_13756;
        
        mem_13756.references = NULL;
        memblock_alloc(ctx, &mem_13756, 8, "mem_13756");
        for (int32_t i_13260 = 0; i_13260 < 2; i_13260++) {
            for (int32_t i_14162 = 0; i_14162 < 2; i_14162++) {
                *(int32_t *) &mem_13756.mem[i_14162 * 4] = i_13260;
            }
            memmove(mem_13751.mem + 2 * i_13260 * 4, mem_13756.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_13756, "mem_13756");
        
        struct memblock mem_13761;
        
        mem_13761.references = NULL;
        memblock_alloc(ctx, &mem_13761, 16, "mem_13761");
        
        struct memblock mem_13764;
        
        mem_13764.references = NULL;
        memblock_alloc(ctx, &mem_13764, 16, "mem_13764");
        
        int32_t discard_13270;
        int32_t scanacc_13264 = 0;
        
        for (int32_t i_13267 = 0; i_13267 < 4; i_13267++) {
            bool not_arg_12717 = i_13267 == 0;
            bool res_12718 = !not_arg_12717;
            int32_t part_res_12719;
            
            if (res_12718) {
                part_res_12719 = 0;
            } else {
                part_res_12719 = 1;
            }
            
            int32_t part_res_12720;
            
            if (res_12718) {
                part_res_12720 = 1;
            } else {
                part_res_12720 = 0;
            }
            
            int32_t zz_12715 = part_res_12720 + scanacc_13264;
            
            *(int32_t *) &mem_13761.mem[i_13267 * 4] = zz_12715;
            *(int32_t *) &mem_13764.mem[i_13267 * 4] = part_res_12719;
            
            int32_t scanacc_tmp_14163 = zz_12715;
            
            scanacc_13264 = scanacc_tmp_14163;
        }
        discard_13270 = scanacc_13264;
        
        int32_t last_offset_12721 = *(int32_t *) &mem_13761.mem[12];
        int64_t binop_x_13774 = sext_i32_i64(last_offset_12721);
        int64_t bytes_13773 = 4 * binop_x_13774;
        struct memblock mem_13775;
        
        mem_13775.references = NULL;
        memblock_alloc(ctx, &mem_13775, bytes_13773, "mem_13775");
        
        struct memblock mem_13778;
        
        mem_13778.references = NULL;
        memblock_alloc(ctx, &mem_13778, bytes_13773, "mem_13778");
        
        struct memblock mem_13781;
        
        mem_13781.references = NULL;
        memblock_alloc(ctx, &mem_13781, bytes_13773, "mem_13781");
        for (int32_t write_iter_13271 = 0; write_iter_13271 < 4;
             write_iter_13271++) {
            int32_t write_iv_13275 =
                    *(int32_t *) &mem_13764.mem[write_iter_13271 * 4];
            int32_t write_iv_13276 =
                    *(int32_t *) &mem_13761.mem[write_iter_13271 * 4];
            int32_t new_index_13584 = squot32(write_iter_13271, 2);
            int32_t binop_y_13586 = 2 * new_index_13584;
            int32_t new_index_13587 = write_iter_13271 - binop_y_13586;
            int32_t write_iv_13277 =
                    *(int32_t *) &mem_13751.mem[(new_index_13584 * 2 +
                                                 new_index_13587) * 4];
            bool is_this_one_12733 = write_iv_13275 == 0;
            int32_t this_offset_12734 = -1 + write_iv_13276;
            int32_t total_res_12735;
            
            if (is_this_one_12733) {
                total_res_12735 = this_offset_12734;
            } else {
                total_res_12735 = -1;
            }
            
            bool less_than_zzero_13280 = slt32(total_res_12735, 0);
            bool greater_than_sizze_13281 = sle32(last_offset_12721,
                                                  total_res_12735);
            bool outside_bounds_dim_13282 = less_than_zzero_13280 ||
                 greater_than_sizze_13281;
            
            if (!outside_bounds_dim_13282) {
                *(int32_t *) &mem_13775.mem[total_res_12735 * 4] =
                    write_iv_13277;
            }
            if (!outside_bounds_dim_13282) {
                *(int32_t *) &mem_13778.mem[total_res_12735 * 4] =
                    new_index_13587;
            }
            if (!outside_bounds_dim_13282) {
                *(int32_t *) &mem_13781.mem[total_res_12735 * 4] =
                    write_iter_13271;
            }
        }
        memblock_unref(ctx, &mem_13751, "mem_13751");
        memblock_unref(ctx, &mem_13761, "mem_13761");
        memblock_unref(ctx, &mem_13764, "mem_13764");
        
        int32_t x_12736 = abs(last_offset_12721);
        bool empty_slice_12737 = x_12736 == 0;
        int32_t m_12738 = x_12736 - 1;
        bool zzero_leq_i_p_m_t_s_12739 = sle32(0, m_12738);
        bool i_p_m_t_s_leq_w_12740 = slt32(m_12738, last_offset_12721);
        bool y_12741 = zzero_leq_i_p_m_t_s_12739 && i_p_m_t_s_leq_w_12740;
        bool ok_or_empty_12742 = empty_slice_12737 || y_12741;
        bool index_certs_12743;
        
        if (!ok_or_empty_12742) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:20-76:24 -> tupleTest.fut:21:13-21:29 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_12721,
                                   "] out of bounds for array of shape [",
                                   last_offset_12721, "].");
            memblock_unref(ctx, &mem_13781, "mem_13781");
            memblock_unref(ctx, &mem_13778, "mem_13778");
            memblock_unref(ctx, &mem_13775, "mem_13775");
            memblock_unref(ctx, &mem_13764, "mem_13764");
            memblock_unref(ctx, &mem_13761, "mem_13761");
            memblock_unref(ctx, &mem_13756, "mem_13756");
            memblock_unref(ctx, &mem_13751, "mem_13751");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        bool dim_eq_12747 = x_12736 == 3;
        bool arrays_equal_12748;
        
        if (dim_eq_12747) {
            bool all_equal_12750;
            bool redout_13298 = 1;
            
            for (int32_t i_13299 = 0; i_13299 < x_12736; i_13299++) {
                int32_t x_12754 = *(int32_t *) &mem_13775.mem[i_13299 * 4];
                int32_t y_12755 = *(int32_t *) &mem_13742.mem[i_13299 * 4];
                bool res_12756 = x_12754 == y_12755;
                bool res_12753 = res_12756 && redout_13298;
                bool redout_tmp_14169 = res_12753;
                
                redout_13298 = redout_tmp_14169;
            }
            all_equal_12750 = redout_13298;
            arrays_equal_12748 = all_equal_12750;
        } else {
            arrays_equal_12748 = 0;
        }
        memblock_unref(ctx, &mem_13775, "mem_13775");
        
        bool arrays_equal_12757;
        
        if (dim_eq_12747) {
            bool all_equal_12759;
            bool redout_13300 = 1;
            
            for (int32_t i_13301 = 0; i_13301 < x_12736; i_13301++) {
                int32_t x_12763 = *(int32_t *) &mem_13778.mem[i_13301 * 4];
                int32_t y_12764 = *(int32_t *) &mem_13745.mem[i_13301 * 4];
                bool res_12765 = x_12763 == y_12764;
                bool res_12762 = res_12765 && redout_13300;
                bool redout_tmp_14170 = res_12762;
                
                redout_13300 = redout_tmp_14170;
            }
            all_equal_12759 = redout_13300;
            arrays_equal_12757 = all_equal_12759;
        } else {
            arrays_equal_12757 = 0;
        }
        memblock_unref(ctx, &mem_13778, "mem_13778");
        
        bool eq_12766 = arrays_equal_12748 && arrays_equal_12757;
        bool res_12767;
        
        if (eq_12766) {
            bool arrays_equal_12768;
            
            if (dim_eq_12747) {
                bool all_equal_12770;
                bool redout_13302 = 1;
                
                for (int32_t i_13303 = 0; i_13303 < x_12736; i_13303++) {
                    int32_t x_12774 = *(int32_t *) &mem_13781.mem[i_13303 * 4];
                    int32_t y_12775 = *(int32_t *) &mem_13748.mem[i_13303 * 4];
                    bool res_12776 = x_12774 == y_12775;
                    bool res_12773 = res_12776 && redout_13302;
                    bool redout_tmp_14171 = res_12773;
                    
                    redout_13302 = redout_tmp_14171;
                }
                all_equal_12770 = redout_13302;
                arrays_equal_12768 = all_equal_12770;
            } else {
                arrays_equal_12768 = 0;
            }
            res_12767 = arrays_equal_12768;
        } else {
            res_12767 = 0;
        }
        memblock_unref(ctx, &mem_13781, "mem_13781");
        cond_12706 = res_12767;
        memblock_unref(ctx, &mem_13781, "mem_13781");
        memblock_unref(ctx, &mem_13778, "mem_13778");
        memblock_unref(ctx, &mem_13775, "mem_13775");
        memblock_unref(ctx, &mem_13764, "mem_13764");
        memblock_unref(ctx, &mem_13761, "mem_13761");
        memblock_unref(ctx, &mem_13756, "mem_13756");
        memblock_unref(ctx, &mem_13751, "mem_13751");
    } else {
        cond_12706 = 0;
    }
    memblock_unref(ctx, &mem_13748, "mem_13748");
    
    bool cond_12777;
    
    if (cond_12706) {
        struct memblock mem_13802;
        
        mem_13802.references = NULL;
        memblock_alloc(ctx, &mem_13802, 16, "mem_13802");
        
        struct memblock mem_13807;
        
        mem_13807.references = NULL;
        memblock_alloc(ctx, &mem_13807, 8, "mem_13807");
        for (int32_t i_13306 = 0; i_13306 < 2; i_13306++) {
            for (int32_t i_14173 = 0; i_14173 < 2; i_14173++) {
                *(int32_t *) &mem_13807.mem[i_14173 * 4] = i_13306;
            }
            memmove(mem_13802.mem + 2 * i_13306 * 4, mem_13807.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_13807, "mem_13807");
        
        struct memblock mem_13812;
        
        mem_13812.references = NULL;
        memblock_alloc(ctx, &mem_13812, 16, "mem_13812");
        
        struct memblock mem_13815;
        
        mem_13815.references = NULL;
        memblock_alloc(ctx, &mem_13815, 16, "mem_13815");
        
        int32_t discard_13316;
        int32_t scanacc_13310 = 0;
        
        for (int32_t i_13313 = 0; i_13313 < 4; i_13313++) {
            bool not_arg_12788 = i_13313 == 0;
            bool res_12789 = !not_arg_12788;
            int32_t part_res_12790;
            
            if (res_12789) {
                part_res_12790 = 0;
            } else {
                part_res_12790 = 1;
            }
            
            int32_t part_res_12791;
            
            if (res_12789) {
                part_res_12791 = 1;
            } else {
                part_res_12791 = 0;
            }
            
            int32_t zz_12786 = part_res_12791 + scanacc_13310;
            
            *(int32_t *) &mem_13812.mem[i_13313 * 4] = zz_12786;
            *(int32_t *) &mem_13815.mem[i_13313 * 4] = part_res_12790;
            
            int32_t scanacc_tmp_14174 = zz_12786;
            
            scanacc_13310 = scanacc_tmp_14174;
        }
        discard_13316 = scanacc_13310;
        
        int32_t last_offset_12792 = *(int32_t *) &mem_13812.mem[12];
        int64_t binop_x_13825 = sext_i32_i64(last_offset_12792);
        int64_t bytes_13824 = 4 * binop_x_13825;
        struct memblock mem_13826;
        
        mem_13826.references = NULL;
        memblock_alloc(ctx, &mem_13826, bytes_13824, "mem_13826");
        
        struct memblock mem_13829;
        
        mem_13829.references = NULL;
        memblock_alloc(ctx, &mem_13829, bytes_13824, "mem_13829");
        
        struct memblock mem_13832;
        
        mem_13832.references = NULL;
        memblock_alloc(ctx, &mem_13832, bytes_13824, "mem_13832");
        for (int32_t write_iter_13317 = 0; write_iter_13317 < 4;
             write_iter_13317++) {
            int32_t write_iv_13321 =
                    *(int32_t *) &mem_13815.mem[write_iter_13317 * 4];
            int32_t write_iv_13322 =
                    *(int32_t *) &mem_13812.mem[write_iter_13317 * 4];
            int32_t new_index_13602 = squot32(write_iter_13317, 2);
            int32_t binop_y_13604 = 2 * new_index_13602;
            int32_t new_index_13605 = write_iter_13317 - binop_y_13604;
            int32_t write_iv_13323 =
                    *(int32_t *) &mem_13802.mem[(new_index_13602 * 2 +
                                                 new_index_13605) * 4];
            bool is_this_one_12804 = write_iv_13321 == 0;
            int32_t this_offset_12805 = -1 + write_iv_13322;
            int32_t total_res_12806;
            
            if (is_this_one_12804) {
                total_res_12806 = this_offset_12805;
            } else {
                total_res_12806 = -1;
            }
            
            bool less_than_zzero_13326 = slt32(total_res_12806, 0);
            bool greater_than_sizze_13327 = sle32(last_offset_12792,
                                                  total_res_12806);
            bool outside_bounds_dim_13328 = less_than_zzero_13326 ||
                 greater_than_sizze_13327;
            
            if (!outside_bounds_dim_13328) {
                *(int32_t *) &mem_13826.mem[total_res_12806 * 4] =
                    write_iv_13323;
            }
            if (!outside_bounds_dim_13328) {
                *(int32_t *) &mem_13829.mem[total_res_12806 * 4] =
                    new_index_13605;
            }
            if (!outside_bounds_dim_13328) {
                *(int32_t *) &mem_13832.mem[total_res_12806 * 4] =
                    write_iter_13317;
            }
        }
        memblock_unref(ctx, &mem_13802, "mem_13802");
        memblock_unref(ctx, &mem_13812, "mem_13812");
        memblock_unref(ctx, &mem_13815, "mem_13815");
        
        int32_t x_12807 = abs(last_offset_12792);
        bool empty_slice_12808 = x_12807 == 0;
        int32_t m_12809 = x_12807 - 1;
        bool zzero_leq_i_p_m_t_s_12810 = sle32(0, m_12809);
        bool i_p_m_t_s_leq_w_12811 = slt32(m_12809, last_offset_12792);
        bool y_12812 = zzero_leq_i_p_m_t_s_12810 && i_p_m_t_s_leq_w_12811;
        bool ok_or_empty_12813 = empty_slice_12808 || y_12812;
        bool index_certs_12814;
        
        if (!ok_or_empty_12813) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:29-76:33 -> tupleTest.fut:27:14-27:30 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_12792,
                                   "] out of bounds for array of shape [",
                                   last_offset_12792, "].");
            memblock_unref(ctx, &mem_13832, "mem_13832");
            memblock_unref(ctx, &mem_13829, "mem_13829");
            memblock_unref(ctx, &mem_13826, "mem_13826");
            memblock_unref(ctx, &mem_13815, "mem_13815");
            memblock_unref(ctx, &mem_13812, "mem_13812");
            memblock_unref(ctx, &mem_13807, "mem_13807");
            memblock_unref(ctx, &mem_13802, "mem_13802");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        struct memblock mem_13853;
        
        mem_13853.references = NULL;
        memblock_alloc(ctx, &mem_13853, 16, "mem_13853");
        for (int32_t i_14180 = 0; i_14180 < 4; i_14180++) {
            *(int32_t *) &mem_13853.mem[i_14180 * 4] = 0;
        }
        for (int32_t write_iter_13344 = 0; write_iter_13344 < x_12807;
             write_iter_13344++) {
            int32_t write_iv_13346 =
                    *(int32_t *) &mem_13826.mem[write_iter_13344 * 4];
            int32_t write_iv_13347 =
                    *(int32_t *) &mem_13829.mem[write_iter_13344 * 4];
            int32_t write_iv_13348 =
                    *(int32_t *) &mem_13832.mem[write_iter_13344 * 4];
            int32_t x_12823 = 2 * write_iv_13346;
            int32_t res_12824 = x_12823 + write_iv_13347;
            bool less_than_zzero_13349 = slt32(res_12824, 0);
            bool greater_than_sizze_13350 = sle32(4, res_12824);
            bool outside_bounds_dim_13351 = less_than_zzero_13349 ||
                 greater_than_sizze_13350;
            
            if (!outside_bounds_dim_13351) {
                *(int32_t *) &mem_13853.mem[res_12824 * 4] = write_iv_13348;
            }
        }
        memblock_unref(ctx, &mem_13826, "mem_13826");
        memblock_unref(ctx, &mem_13829, "mem_13829");
        memblock_unref(ctx, &mem_13832, "mem_13832");
        
        bool all_equal_12825;
        bool redout_13355 = 1;
        
        for (int32_t i_13356 = 0; i_13356 < 4; i_13356++) {
            int32_t y_12830 = *(int32_t *) &mem_13853.mem[i_13356 * 4];
            bool res_12831 = i_13356 == y_12830;
            bool res_12828 = res_12831 && redout_13355;
            bool redout_tmp_14182 = res_12828;
            
            redout_13355 = redout_tmp_14182;
        }
        all_equal_12825 = redout_13355;
        memblock_unref(ctx, &mem_13853, "mem_13853");
        cond_12777 = all_equal_12825;
        memblock_unref(ctx, &mem_13853, "mem_13853");
        memblock_unref(ctx, &mem_13832, "mem_13832");
        memblock_unref(ctx, &mem_13829, "mem_13829");
        memblock_unref(ctx, &mem_13826, "mem_13826");
        memblock_unref(ctx, &mem_13815, "mem_13815");
        memblock_unref(ctx, &mem_13812, "mem_13812");
        memblock_unref(ctx, &mem_13807, "mem_13807");
        memblock_unref(ctx, &mem_13802, "mem_13802");
    } else {
        cond_12777 = 0;
    }
    
    struct memblock mem_13862;
    
    mem_13862.references = NULL;
    memblock_alloc(ctx, &mem_13862, 12, "mem_13862");
    
    struct memblock static_array_14183 = ctx->static_array_14183;
    
    memmove(mem_13862.mem + 0, static_array_14183.mem + 0, 3 * sizeof(int32_t));
    
    bool cond_12851;
    
    if (cond_12777) {
        struct memblock mem_13865;
        
        mem_13865.references = NULL;
        memblock_alloc(ctx, &mem_13865, 16, "mem_13865");
        
        struct memblock mem_13870;
        
        mem_13870.references = NULL;
        memblock_alloc(ctx, &mem_13870, 8, "mem_13870");
        for (int32_t i_13372 = 0; i_13372 < 2; i_13372++) {
            for (int32_t i_14185 = 0; i_14185 < 2; i_14185++) {
                *(int32_t *) &mem_13870.mem[i_14185 * 4] = i_13372;
            }
            memmove(mem_13865.mem + 2 * i_13372 * 4, mem_13870.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_13870, "mem_13870");
        
        struct memblock mem_13875;
        
        mem_13875.references = NULL;
        memblock_alloc(ctx, &mem_13875, 16, "mem_13875");
        
        struct memblock mem_13878;
        
        mem_13878.references = NULL;
        memblock_alloc(ctx, &mem_13878, 16, "mem_13878");
        
        int32_t discard_13382;
        int32_t scanacc_13376 = 0;
        
        for (int32_t i_13379 = 0; i_13379 < 4; i_13379++) {
            bool not_arg_12862 = i_13379 == 0;
            bool res_12863 = !not_arg_12862;
            int32_t part_res_12864;
            
            if (res_12863) {
                part_res_12864 = 0;
            } else {
                part_res_12864 = 1;
            }
            
            int32_t part_res_12865;
            
            if (res_12863) {
                part_res_12865 = 1;
            } else {
                part_res_12865 = 0;
            }
            
            int32_t zz_12860 = part_res_12865 + scanacc_13376;
            
            *(int32_t *) &mem_13875.mem[i_13379 * 4] = zz_12860;
            *(int32_t *) &mem_13878.mem[i_13379 * 4] = part_res_12864;
            
            int32_t scanacc_tmp_14186 = zz_12860;
            
            scanacc_13376 = scanacc_tmp_14186;
        }
        discard_13382 = scanacc_13376;
        
        int32_t last_offset_12866 = *(int32_t *) &mem_13875.mem[12];
        int64_t binop_x_13888 = sext_i32_i64(last_offset_12866);
        int64_t bytes_13887 = 4 * binop_x_13888;
        struct memblock mem_13889;
        
        mem_13889.references = NULL;
        memblock_alloc(ctx, &mem_13889, bytes_13887, "mem_13889");
        
        struct memblock mem_13892;
        
        mem_13892.references = NULL;
        memblock_alloc(ctx, &mem_13892, bytes_13887, "mem_13892");
        
        struct memblock mem_13895;
        
        mem_13895.references = NULL;
        memblock_alloc(ctx, &mem_13895, bytes_13887, "mem_13895");
        for (int32_t write_iter_13383 = 0; write_iter_13383 < 4;
             write_iter_13383++) {
            int32_t write_iv_13387 =
                    *(int32_t *) &mem_13878.mem[write_iter_13383 * 4];
            int32_t write_iv_13388 =
                    *(int32_t *) &mem_13875.mem[write_iter_13383 * 4];
            int32_t new_index_13621 = squot32(write_iter_13383, 2);
            int32_t binop_y_13623 = 2 * new_index_13621;
            int32_t new_index_13624 = write_iter_13383 - binop_y_13623;
            int32_t write_iv_13389 =
                    *(int32_t *) &mem_13865.mem[(new_index_13621 * 2 +
                                                 new_index_13624) * 4];
            bool is_this_one_12878 = write_iv_13387 == 0;
            int32_t this_offset_12879 = -1 + write_iv_13388;
            int32_t total_res_12880;
            
            if (is_this_one_12878) {
                total_res_12880 = this_offset_12879;
            } else {
                total_res_12880 = -1;
            }
            
            bool less_than_zzero_13392 = slt32(total_res_12880, 0);
            bool greater_than_sizze_13393 = sle32(last_offset_12866,
                                                  total_res_12880);
            bool outside_bounds_dim_13394 = less_than_zzero_13392 ||
                 greater_than_sizze_13393;
            
            if (!outside_bounds_dim_13394) {
                *(int32_t *) &mem_13889.mem[total_res_12880 * 4] =
                    write_iv_13389;
            }
            if (!outside_bounds_dim_13394) {
                *(int32_t *) &mem_13892.mem[total_res_12880 * 4] =
                    new_index_13624;
            }
            if (!outside_bounds_dim_13394) {
                *(int32_t *) &mem_13895.mem[total_res_12880 * 4] =
                    write_iter_13383;
            }
        }
        memblock_unref(ctx, &mem_13865, "mem_13865");
        memblock_unref(ctx, &mem_13875, "mem_13875");
        memblock_unref(ctx, &mem_13878, "mem_13878");
        
        int32_t x_12881 = abs(last_offset_12866);
        bool empty_slice_12882 = x_12881 == 0;
        int32_t m_12883 = x_12881 - 1;
        bool zzero_leq_i_p_m_t_s_12884 = sle32(0, m_12883);
        bool i_p_m_t_s_leq_w_12885 = slt32(m_12883, last_offset_12866);
        bool y_12886 = zzero_leq_i_p_m_t_s_12884 && i_p_m_t_s_leq_w_12885;
        bool ok_or_empty_12887 = empty_slice_12882 || y_12886;
        bool index_certs_12888;
        
        if (!ok_or_empty_12887) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:56-76:60 -> tupleTest.fut:50:13-50:29 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_12866,
                                   "] out of bounds for array of shape [",
                                   last_offset_12866, "].");
            memblock_unref(ctx, &mem_13895, "mem_13895");
            memblock_unref(ctx, &mem_13892, "mem_13892");
            memblock_unref(ctx, &mem_13889, "mem_13889");
            memblock_unref(ctx, &mem_13878, "mem_13878");
            memblock_unref(ctx, &mem_13875, "mem_13875");
            memblock_unref(ctx, &mem_13870, "mem_13870");
            memblock_unref(ctx, &mem_13865, "mem_13865");
            memblock_unref(ctx, &mem_13862, "mem_13862");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        struct memblock mem_13916;
        
        mem_13916.references = NULL;
        memblock_alloc(ctx, &mem_13916, 4, "mem_13916");
        for (int32_t i_14192 = 0; i_14192 < 1; i_14192++) {
            *(int32_t *) &mem_13916.mem[i_14192 * 4] = 1;
        }
        
        struct memblock mem_13919;
        
        mem_13919.references = NULL;
        memblock_alloc(ctx, &mem_13919, 4, "mem_13919");
        for (int32_t i_14193 = 0; i_14193 < 1; i_14193++) {
            *(int32_t *) &mem_13919.mem[i_14193 * 4] = 0;
        }
        
        struct memblock mem_13922;
        
        mem_13922.references = NULL;
        memblock_alloc(ctx, &mem_13922, 4, "mem_13922");
        for (int32_t i_14194 = 0; i_14194 < 1; i_14194++) {
            *(int32_t *) &mem_13922.mem[i_14194 * 4] = 4;
        }
        
        int32_t conc_tmp_12896 = 1 + x_12881;
        int32_t res_12897;
        int32_t redout_13410 = x_12881;
        
        for (int32_t i_13411 = 0; i_13411 < x_12881; i_13411++) {
            int32_t x_12901 = *(int32_t *) &mem_13889.mem[i_13411 * 4];
            int32_t x_12902 = *(int32_t *) &mem_13892.mem[i_13411 * 4];
            bool cond_12904 = x_12901 == 1;
            bool cond_12905 = x_12902 == 0;
            bool eq_12906 = cond_12904 && cond_12905;
            int32_t res_12907;
            
            if (eq_12906) {
                res_12907 = i_13411;
            } else {
                res_12907 = x_12881;
            }
            
            int32_t res_12900 = smin32(res_12907, redout_13410);
            int32_t redout_tmp_14195 = res_12900;
            
            redout_13410 = redout_tmp_14195;
        }
        res_12897 = redout_13410;
        
        bool cond_12908 = res_12897 == x_12881;
        int32_t res_12909;
        
        if (cond_12908) {
            res_12909 = -1;
        } else {
            res_12909 = res_12897;
        }
        
        bool eq_x_zz_12910 = -1 == res_12897;
        bool not_p_12911 = !cond_12908;
        bool p_and_eq_x_y_12912 = eq_x_zz_12910 && not_p_12911;
        bool cond_12913 = cond_12908 || p_and_eq_x_y_12912;
        bool cond_12914 = !cond_12913;
        int32_t sizze_12915;
        
        if (cond_12914) {
            sizze_12915 = x_12881;
        } else {
            sizze_12915 = conc_tmp_12896;
        }
        
        int64_t binop_x_13924 = sext_i32_i64(x_12881);
        int64_t bytes_13923 = 4 * binop_x_13924;
        int64_t binop_x_13929 = sext_i32_i64(sizze_12915);
        int64_t bytes_13928 = 4 * binop_x_13929;
        int64_t binop_x_13937 = sext_i32_i64(conc_tmp_12896);
        int64_t bytes_13936 = 4 * binop_x_13937;
        int64_t res_mem_sizze_13945;
        struct memblock res_mem_13946;
        
        res_mem_13946.references = NULL;
        
        int64_t res_mem_sizze_13947;
        struct memblock res_mem_13948;
        
        res_mem_13948.references = NULL;
        
        int64_t res_mem_sizze_13949;
        struct memblock res_mem_13950;
        
        res_mem_13950.references = NULL;
        if (cond_12914) {
            struct memblock mem_13925;
            
            mem_13925.references = NULL;
            memblock_alloc(ctx, &mem_13925, bytes_13923, "mem_13925");
            memmove(mem_13925.mem + 0, mem_13895.mem + 0, x_12881 *
                    sizeof(int32_t));
            
            bool less_than_zzero_13414 = slt32(res_12909, 0);
            bool greater_than_sizze_13415 = sle32(x_12881, res_12909);
            bool outside_bounds_dim_13416 = less_than_zzero_13414 ||
                 greater_than_sizze_13415;
            
            if (!outside_bounds_dim_13416) {
                *(int32_t *) &mem_13925.mem[res_12909 * 4] = 4;
            }
            
            struct memblock mem_13930;
            
            mem_13930.references = NULL;
            memblock_alloc(ctx, &mem_13930, bytes_13928, "mem_13930");
            memmove(mem_13930.mem + 0, mem_13889.mem + 0, sizze_12915 *
                    sizeof(int32_t));
            
            struct memblock mem_13934;
            
            mem_13934.references = NULL;
            memblock_alloc(ctx, &mem_13934, bytes_13928, "mem_13934");
            memmove(mem_13934.mem + 0, mem_13892.mem + 0, sizze_12915 *
                    sizeof(int32_t));
            res_mem_sizze_13945 = bytes_13928;
            memblock_set(ctx, &res_mem_13946, &mem_13930, "mem_13930");
            res_mem_sizze_13947 = bytes_13928;
            memblock_set(ctx, &res_mem_13948, &mem_13934, "mem_13934");
            res_mem_sizze_13949 = bytes_13923;
            memblock_set(ctx, &res_mem_13950, &mem_13925, "mem_13925");
            memblock_unref(ctx, &mem_13934, "mem_13934");
            memblock_unref(ctx, &mem_13930, "mem_13930");
            memblock_unref(ctx, &mem_13925, "mem_13925");
        } else {
            struct memblock mem_13938;
            
            mem_13938.references = NULL;
            memblock_alloc(ctx, &mem_13938, bytes_13936, "mem_13938");
            
            int32_t tmp_offs_14196 = 0;
            
            memmove(mem_13938.mem + tmp_offs_14196 * 4, mem_13889.mem + 0,
                    x_12881 * sizeof(int32_t));
            tmp_offs_14196 += x_12881;
            memmove(mem_13938.mem + tmp_offs_14196 * 4, mem_13916.mem + 0,
                    sizeof(int32_t));
            tmp_offs_14196 += 1;
            
            struct memblock mem_13941;
            
            mem_13941.references = NULL;
            memblock_alloc(ctx, &mem_13941, bytes_13936, "mem_13941");
            
            int32_t tmp_offs_14197 = 0;
            
            memmove(mem_13941.mem + tmp_offs_14197 * 4, mem_13892.mem + 0,
                    x_12881 * sizeof(int32_t));
            tmp_offs_14197 += x_12881;
            memmove(mem_13941.mem + tmp_offs_14197 * 4, mem_13919.mem + 0,
                    sizeof(int32_t));
            tmp_offs_14197 += 1;
            
            struct memblock mem_13944;
            
            mem_13944.references = NULL;
            memblock_alloc(ctx, &mem_13944, bytes_13936, "mem_13944");
            
            int32_t tmp_offs_14198 = 0;
            
            memmove(mem_13944.mem + tmp_offs_14198 * 4, mem_13895.mem + 0,
                    x_12881 * sizeof(int32_t));
            tmp_offs_14198 += x_12881;
            memmove(mem_13944.mem + tmp_offs_14198 * 4, mem_13922.mem + 0,
                    sizeof(int32_t));
            tmp_offs_14198 += 1;
            res_mem_sizze_13945 = bytes_13936;
            memblock_set(ctx, &res_mem_13946, &mem_13938, "mem_13938");
            res_mem_sizze_13947 = bytes_13936;
            memblock_set(ctx, &res_mem_13948, &mem_13941, "mem_13941");
            res_mem_sizze_13949 = bytes_13936;
            memblock_set(ctx, &res_mem_13950, &mem_13944, "mem_13944");
            memblock_unref(ctx, &mem_13944, "mem_13944");
            memblock_unref(ctx, &mem_13941, "mem_13941");
            memblock_unref(ctx, &mem_13938, "mem_13938");
        }
        memblock_unref(ctx, &mem_13889, "mem_13889");
        memblock_unref(ctx, &mem_13892, "mem_13892");
        memblock_unref(ctx, &mem_13895, "mem_13895");
        memblock_unref(ctx, &mem_13916, "mem_13916");
        memblock_unref(ctx, &mem_13919, "mem_13919");
        memblock_unref(ctx, &mem_13922, "mem_13922");
        
        bool eq_x_y_12930 = 0 == x_12881;
        bool p_and_eq_x_y_12931 = cond_12914 && eq_x_y_12930;
        bool both_empty_12932 = p_and_eq_x_y_12931 && p_and_eq_x_y_12931;
        bool p_and_eq_x_y_12933 = cond_12914 && cond_12914;
        bool p_and_eq_x_y_12934 = cond_12913 && cond_12913;
        bool dim_match_12935 = p_and_eq_x_y_12933 || p_and_eq_x_y_12934;
        bool empty_or_match_12936 = both_empty_12932 || dim_match_12935;
        bool empty_or_match_cert_12937;
        
        if (!empty_or_match_12936) {
            ctx->error = msgprintf("Error at %s:\n%s%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:56-76:60 -> tupleTest.fut:51:13-51:34 -> tupleSparse.fut:47:1-54:76",
                                   "Function return value does not match shape of type ",
                                   "matrix");
            memblock_unref(ctx, &res_mem_13950, "res_mem_13950");
            memblock_unref(ctx, &res_mem_13948, "res_mem_13948");
            memblock_unref(ctx, &res_mem_13946, "res_mem_13946");
            memblock_unref(ctx, &mem_13922, "mem_13922");
            memblock_unref(ctx, &mem_13919, "mem_13919");
            memblock_unref(ctx, &mem_13916, "mem_13916");
            memblock_unref(ctx, &mem_13895, "mem_13895");
            memblock_unref(ctx, &mem_13892, "mem_13892");
            memblock_unref(ctx, &mem_13889, "mem_13889");
            memblock_unref(ctx, &mem_13878, "mem_13878");
            memblock_unref(ctx, &mem_13875, "mem_13875");
            memblock_unref(ctx, &mem_13870, "mem_13870");
            memblock_unref(ctx, &mem_13865, "mem_13865");
            memblock_unref(ctx, &mem_13862, "mem_13862");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        bool eq_x_y_12938 = 3 == x_12881;
        bool eq_x_zz_12939 = 3 == conc_tmp_12896;
        bool p_and_eq_x_y_12940 = cond_12914 && eq_x_y_12938;
        bool p_and_eq_x_y_12941 = cond_12913 && eq_x_zz_12939;
        bool dim_eq_12942 = p_and_eq_x_y_12940 || p_and_eq_x_y_12941;
        bool arrays_equal_12943;
        
        if (dim_eq_12942) {
            bool all_equal_12945;
            bool redout_13420 = 1;
            
            for (int32_t i_13421 = 0; i_13421 < sizze_12915; i_13421++) {
                int32_t x_12949 = *(int32_t *) &res_mem_13950.mem[i_13421 * 4];
                int32_t y_12950 = *(int32_t *) &mem_13862.mem[i_13421 * 4];
                bool res_12951 = x_12949 == y_12950;
                bool res_12948 = res_12951 && redout_13420;
                bool redout_tmp_14199 = res_12948;
                
                redout_13420 = redout_tmp_14199;
            }
            all_equal_12945 = redout_13420;
            arrays_equal_12943 = all_equal_12945;
        } else {
            arrays_equal_12943 = 0;
        }
        memblock_unref(ctx, &res_mem_13950, "res_mem_13950");
        
        bool res_12952;
        
        if (arrays_equal_12943) {
            bool arrays_equal_12953;
            
            if (dim_eq_12942) {
                bool all_equal_12955;
                bool redout_13422 = 1;
                
                for (int32_t i_13423 = 0; i_13423 < sizze_12915; i_13423++) {
                    int32_t x_12959 = *(int32_t *) &res_mem_13946.mem[i_13423 *
                                                                      4];
                    int32_t y_12960 = *(int32_t *) &mem_13742.mem[i_13423 * 4];
                    bool res_12961 = x_12959 == y_12960;
                    bool res_12958 = res_12961 && redout_13422;
                    bool redout_tmp_14200 = res_12958;
                    
                    redout_13422 = redout_tmp_14200;
                }
                all_equal_12955 = redout_13422;
                arrays_equal_12953 = all_equal_12955;
            } else {
                arrays_equal_12953 = 0;
            }
            
            bool arrays_equal_12962;
            
            if (dim_eq_12942) {
                bool all_equal_12964;
                bool redout_13424 = 1;
                
                for (int32_t i_13425 = 0; i_13425 < sizze_12915; i_13425++) {
                    int32_t x_12968 = *(int32_t *) &res_mem_13948.mem[i_13425 *
                                                                      4];
                    int32_t y_12969 = *(int32_t *) &mem_13745.mem[i_13425 * 4];
                    bool res_12970 = x_12968 == y_12969;
                    bool res_12967 = res_12970 && redout_13424;
                    bool redout_tmp_14201 = res_12967;
                    
                    redout_13424 = redout_tmp_14201;
                }
                all_equal_12964 = redout_13424;
                arrays_equal_12962 = all_equal_12964;
            } else {
                arrays_equal_12962 = 0;
            }
            
            bool eq_12971 = arrays_equal_12953 && arrays_equal_12962;
            
            res_12952 = eq_12971;
        } else {
            res_12952 = 0;
        }
        memblock_unref(ctx, &res_mem_13946, "res_mem_13946");
        memblock_unref(ctx, &res_mem_13948, "res_mem_13948");
        cond_12851 = res_12952;
        memblock_unref(ctx, &res_mem_13950, "res_mem_13950");
        memblock_unref(ctx, &res_mem_13948, "res_mem_13948");
        memblock_unref(ctx, &res_mem_13946, "res_mem_13946");
        memblock_unref(ctx, &mem_13922, "mem_13922");
        memblock_unref(ctx, &mem_13919, "mem_13919");
        memblock_unref(ctx, &mem_13916, "mem_13916");
        memblock_unref(ctx, &mem_13895, "mem_13895");
        memblock_unref(ctx, &mem_13892, "mem_13892");
        memblock_unref(ctx, &mem_13889, "mem_13889");
        memblock_unref(ctx, &mem_13878, "mem_13878");
        memblock_unref(ctx, &mem_13875, "mem_13875");
        memblock_unref(ctx, &mem_13870, "mem_13870");
        memblock_unref(ctx, &mem_13865, "mem_13865");
    } else {
        cond_12851 = 0;
    }
    memblock_unref(ctx, &mem_13742, "mem_13742");
    memblock_unref(ctx, &mem_13745, "mem_13745");
    memblock_unref(ctx, &mem_13862, "mem_13862");
    
    struct memblock mem_13953;
    
    mem_13953.references = NULL;
    memblock_alloc(ctx, &mem_13953, 16, "mem_13953");
    
    struct memblock static_array_14202 = ctx->static_array_14202;
    
    memmove(mem_13953.mem + 0, static_array_14202.mem + 0, 4 * sizeof(int32_t));
    
    struct memblock mem_13956;
    
    mem_13956.references = NULL;
    memblock_alloc(ctx, &mem_13956, 16, "mem_13956");
    
    struct memblock static_array_14203 = ctx->static_array_14203;
    
    memmove(mem_13956.mem + 0, static_array_14203.mem + 0, 4 * sizeof(int32_t));
    
    bool res_12974;
    
    if (cond_12851) {
        struct memblock mem_13959;
        
        mem_13959.references = NULL;
        memblock_alloc(ctx, &mem_13959, 16, "mem_13959");
        
        struct memblock mem_13964;
        
        mem_13964.references = NULL;
        memblock_alloc(ctx, &mem_13964, 8, "mem_13964");
        for (int32_t i_13428 = 0; i_13428 < 2; i_13428++) {
            for (int32_t i_14205 = 0; i_14205 < 2; i_14205++) {
                *(int32_t *) &mem_13964.mem[i_14205 * 4] = i_13428;
            }
            memmove(mem_13959.mem + 2 * i_13428 * 4, mem_13964.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_13964, "mem_13964");
        
        struct memblock mem_13969;
        
        mem_13969.references = NULL;
        memblock_alloc(ctx, &mem_13969, 16, "mem_13969");
        
        struct memblock mem_13972;
        
        mem_13972.references = NULL;
        memblock_alloc(ctx, &mem_13972, 16, "mem_13972");
        
        int32_t discard_13438;
        int32_t scanacc_13432 = 0;
        
        for (int32_t i_13435 = 0; i_13435 < 4; i_13435++) {
            bool not_arg_12985 = i_13435 == 0;
            bool res_12986 = !not_arg_12985;
            int32_t part_res_12987;
            
            if (res_12986) {
                part_res_12987 = 0;
            } else {
                part_res_12987 = 1;
            }
            
            int32_t part_res_12988;
            
            if (res_12986) {
                part_res_12988 = 1;
            } else {
                part_res_12988 = 0;
            }
            
            int32_t zz_12983 = part_res_12988 + scanacc_13432;
            
            *(int32_t *) &mem_13969.mem[i_13435 * 4] = zz_12983;
            *(int32_t *) &mem_13972.mem[i_13435 * 4] = part_res_12987;
            
            int32_t scanacc_tmp_14206 = zz_12983;
            
            scanacc_13432 = scanacc_tmp_14206;
        }
        discard_13438 = scanacc_13432;
        
        int32_t last_offset_12989 = *(int32_t *) &mem_13969.mem[12];
        int64_t binop_x_13982 = sext_i32_i64(last_offset_12989);
        int64_t bytes_13981 = 4 * binop_x_13982;
        struct memblock mem_13983;
        
        mem_13983.references = NULL;
        memblock_alloc(ctx, &mem_13983, bytes_13981, "mem_13983");
        
        struct memblock mem_13986;
        
        mem_13986.references = NULL;
        memblock_alloc(ctx, &mem_13986, bytes_13981, "mem_13986");
        
        struct memblock mem_13989;
        
        mem_13989.references = NULL;
        memblock_alloc(ctx, &mem_13989, bytes_13981, "mem_13989");
        for (int32_t write_iter_13439 = 0; write_iter_13439 < 4;
             write_iter_13439++) {
            int32_t write_iv_13443 =
                    *(int32_t *) &mem_13972.mem[write_iter_13439 * 4];
            int32_t write_iv_13444 =
                    *(int32_t *) &mem_13969.mem[write_iter_13439 * 4];
            int32_t new_index_13638 = squot32(write_iter_13439, 2);
            int32_t binop_y_13640 = 2 * new_index_13638;
            int32_t new_index_13641 = write_iter_13439 - binop_y_13640;
            int32_t write_iv_13445 =
                    *(int32_t *) &mem_13959.mem[(new_index_13638 * 2 +
                                                 new_index_13641) * 4];
            bool is_this_one_13001 = write_iv_13443 == 0;
            int32_t this_offset_13002 = -1 + write_iv_13444;
            int32_t total_res_13003;
            
            if (is_this_one_13001) {
                total_res_13003 = this_offset_13002;
            } else {
                total_res_13003 = -1;
            }
            
            bool less_than_zzero_13448 = slt32(total_res_13003, 0);
            bool greater_than_sizze_13449 = sle32(last_offset_12989,
                                                  total_res_13003);
            bool outside_bounds_dim_13450 = less_than_zzero_13448 ||
                 greater_than_sizze_13449;
            
            if (!outside_bounds_dim_13450) {
                *(int32_t *) &mem_13983.mem[total_res_13003 * 4] =
                    write_iv_13445;
            }
            if (!outside_bounds_dim_13450) {
                *(int32_t *) &mem_13986.mem[total_res_13003 * 4] =
                    new_index_13641;
            }
            if (!outside_bounds_dim_13450) {
                *(int32_t *) &mem_13989.mem[total_res_13003 * 4] =
                    write_iter_13439;
            }
        }
        memblock_unref(ctx, &mem_13959, "mem_13959");
        memblock_unref(ctx, &mem_13969, "mem_13969");
        memblock_unref(ctx, &mem_13972, "mem_13972");
        
        int32_t x_13004 = abs(last_offset_12989);
        bool empty_slice_13005 = x_13004 == 0;
        int32_t m_13006 = x_13004 - 1;
        bool zzero_leq_i_p_m_t_s_13007 = sle32(0, m_13006);
        bool i_p_m_t_s_leq_w_13008 = slt32(m_13006, last_offset_12989);
        bool y_13009 = zzero_leq_i_p_m_t_s_13007 && i_p_m_t_s_leq_w_13008;
        bool ok_or_empty_13010 = empty_slice_13005 || y_13009;
        bool index_certs_13011;
        
        if (!ok_or_empty_13010) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:65-76:69 -> tupleTest.fut:57:13-57:29 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_12989,
                                   "] out of bounds for array of shape [",
                                   last_offset_12989, "].");
            memblock_unref(ctx, &mem_13989, "mem_13989");
            memblock_unref(ctx, &mem_13986, "mem_13986");
            memblock_unref(ctx, &mem_13983, "mem_13983");
            memblock_unref(ctx, &mem_13972, "mem_13972");
            memblock_unref(ctx, &mem_13969, "mem_13969");
            memblock_unref(ctx, &mem_13964, "mem_13964");
            memblock_unref(ctx, &mem_13959, "mem_13959");
            memblock_unref(ctx, &mem_13956, "mem_13956");
            memblock_unref(ctx, &mem_13953, "mem_13953");
            memblock_unref(ctx, &mem_13862, "mem_13862");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        struct memblock mem_14010;
        
        mem_14010.references = NULL;
        memblock_alloc(ctx, &mem_14010, 4, "mem_14010");
        for (int32_t i_14212 = 0; i_14212 < 1; i_14212++) {
            *(int32_t *) &mem_14010.mem[i_14212 * 4] = 0;
        }
        
        struct memblock mem_14013;
        
        mem_14013.references = NULL;
        memblock_alloc(ctx, &mem_14013, 4, "mem_14013");
        for (int32_t i_14213 = 0; i_14213 < 1; i_14213++) {
            *(int32_t *) &mem_14013.mem[i_14213 * 4] = 4;
        }
        
        int32_t conc_tmp_13018 = 1 + x_13004;
        int32_t res_13019;
        int32_t redout_13466 = x_13004;
        
        for (int32_t i_13467 = 0; i_13467 < x_13004; i_13467++) {
            int32_t x_13023 = *(int32_t *) &mem_13983.mem[i_13467 * 4];
            int32_t x_13024 = *(int32_t *) &mem_13986.mem[i_13467 * 4];
            bool cond_13026 = x_13023 == 0;
            bool cond_13027 = x_13024 == 0;
            bool eq_13028 = cond_13026 && cond_13027;
            int32_t res_13029;
            
            if (eq_13028) {
                res_13029 = i_13467;
            } else {
                res_13029 = x_13004;
            }
            
            int32_t res_13022 = smin32(res_13029, redout_13466);
            int32_t redout_tmp_14214 = res_13022;
            
            redout_13466 = redout_tmp_14214;
        }
        res_13019 = redout_13466;
        
        bool cond_13030 = res_13019 == x_13004;
        int32_t res_13031;
        
        if (cond_13030) {
            res_13031 = -1;
        } else {
            res_13031 = res_13019;
        }
        
        bool eq_x_zz_13032 = -1 == res_13019;
        bool not_p_13033 = !cond_13030;
        bool p_and_eq_x_y_13034 = eq_x_zz_13032 && not_p_13033;
        bool cond_13035 = cond_13030 || p_and_eq_x_y_13034;
        bool cond_13036 = !cond_13035;
        int32_t sizze_13037;
        
        if (cond_13036) {
            sizze_13037 = x_13004;
        } else {
            sizze_13037 = conc_tmp_13018;
        }
        
        int64_t binop_x_14015 = sext_i32_i64(x_13004);
        int64_t bytes_14014 = 4 * binop_x_14015;
        int64_t binop_x_14020 = sext_i32_i64(sizze_13037);
        int64_t bytes_14019 = 4 * binop_x_14020;
        int64_t binop_x_14028 = sext_i32_i64(conc_tmp_13018);
        int64_t bytes_14027 = 4 * binop_x_14028;
        int64_t res_mem_sizze_14036;
        struct memblock res_mem_14037;
        
        res_mem_14037.references = NULL;
        
        int64_t res_mem_sizze_14038;
        struct memblock res_mem_14039;
        
        res_mem_14039.references = NULL;
        
        int64_t res_mem_sizze_14040;
        struct memblock res_mem_14041;
        
        res_mem_14041.references = NULL;
        if (cond_13036) {
            struct memblock mem_14016;
            
            mem_14016.references = NULL;
            memblock_alloc(ctx, &mem_14016, bytes_14014, "mem_14016");
            memmove(mem_14016.mem + 0, mem_13989.mem + 0, x_13004 *
                    sizeof(int32_t));
            
            bool less_than_zzero_13470 = slt32(res_13031, 0);
            bool greater_than_sizze_13471 = sle32(x_13004, res_13031);
            bool outside_bounds_dim_13472 = less_than_zzero_13470 ||
                 greater_than_sizze_13471;
            
            if (!outside_bounds_dim_13472) {
                *(int32_t *) &mem_14016.mem[res_13031 * 4] = 4;
            }
            
            struct memblock mem_14021;
            
            mem_14021.references = NULL;
            memblock_alloc(ctx, &mem_14021, bytes_14019, "mem_14021");
            memmove(mem_14021.mem + 0, mem_13983.mem + 0, sizze_13037 *
                    sizeof(int32_t));
            
            struct memblock mem_14025;
            
            mem_14025.references = NULL;
            memblock_alloc(ctx, &mem_14025, bytes_14019, "mem_14025");
            memmove(mem_14025.mem + 0, mem_13986.mem + 0, sizze_13037 *
                    sizeof(int32_t));
            res_mem_sizze_14036 = bytes_14019;
            memblock_set(ctx, &res_mem_14037, &mem_14021, "mem_14021");
            res_mem_sizze_14038 = bytes_14019;
            memblock_set(ctx, &res_mem_14039, &mem_14025, "mem_14025");
            res_mem_sizze_14040 = bytes_14014;
            memblock_set(ctx, &res_mem_14041, &mem_14016, "mem_14016");
            memblock_unref(ctx, &mem_14025, "mem_14025");
            memblock_unref(ctx, &mem_14021, "mem_14021");
            memblock_unref(ctx, &mem_14016, "mem_14016");
        } else {
            struct memblock mem_14029;
            
            mem_14029.references = NULL;
            memblock_alloc(ctx, &mem_14029, bytes_14027, "mem_14029");
            
            int32_t tmp_offs_14215 = 0;
            
            memmove(mem_14029.mem + tmp_offs_14215 * 4, mem_13983.mem + 0,
                    x_13004 * sizeof(int32_t));
            tmp_offs_14215 += x_13004;
            memmove(mem_14029.mem + tmp_offs_14215 * 4, mem_14010.mem + 0,
                    sizeof(int32_t));
            tmp_offs_14215 += 1;
            
            struct memblock mem_14032;
            
            mem_14032.references = NULL;
            memblock_alloc(ctx, &mem_14032, bytes_14027, "mem_14032");
            
            int32_t tmp_offs_14216 = 0;
            
            memmove(mem_14032.mem + tmp_offs_14216 * 4, mem_13986.mem + 0,
                    x_13004 * sizeof(int32_t));
            tmp_offs_14216 += x_13004;
            memmove(mem_14032.mem + tmp_offs_14216 * 4, mem_14010.mem + 0,
                    sizeof(int32_t));
            tmp_offs_14216 += 1;
            
            struct memblock mem_14035;
            
            mem_14035.references = NULL;
            memblock_alloc(ctx, &mem_14035, bytes_14027, "mem_14035");
            
            int32_t tmp_offs_14217 = 0;
            
            memmove(mem_14035.mem + tmp_offs_14217 * 4, mem_13989.mem + 0,
                    x_13004 * sizeof(int32_t));
            tmp_offs_14217 += x_13004;
            memmove(mem_14035.mem + tmp_offs_14217 * 4, mem_14013.mem + 0,
                    sizeof(int32_t));
            tmp_offs_14217 += 1;
            res_mem_sizze_14036 = bytes_14027;
            memblock_set(ctx, &res_mem_14037, &mem_14029, "mem_14029");
            res_mem_sizze_14038 = bytes_14027;
            memblock_set(ctx, &res_mem_14039, &mem_14032, "mem_14032");
            res_mem_sizze_14040 = bytes_14027;
            memblock_set(ctx, &res_mem_14041, &mem_14035, "mem_14035");
            memblock_unref(ctx, &mem_14035, "mem_14035");
            memblock_unref(ctx, &mem_14032, "mem_14032");
            memblock_unref(ctx, &mem_14029, "mem_14029");
        }
        memblock_unref(ctx, &mem_13983, "mem_13983");
        memblock_unref(ctx, &mem_13986, "mem_13986");
        memblock_unref(ctx, &mem_13989, "mem_13989");
        memblock_unref(ctx, &mem_14010, "mem_14010");
        memblock_unref(ctx, &mem_14013, "mem_14013");
        
        bool eq_x_y_13052 = 0 == x_13004;
        bool p_and_eq_x_y_13053 = cond_13036 && eq_x_y_13052;
        bool both_empty_13054 = p_and_eq_x_y_13053 && p_and_eq_x_y_13053;
        bool p_and_eq_x_y_13055 = cond_13036 && cond_13036;
        bool p_and_eq_x_y_13056 = cond_13035 && cond_13035;
        bool dim_match_13057 = p_and_eq_x_y_13055 || p_and_eq_x_y_13056;
        bool empty_or_match_13058 = both_empty_13054 || dim_match_13057;
        bool empty_or_match_cert_13059;
        
        if (!empty_or_match_13058) {
            ctx->error = msgprintf("Error at %s:\n%s%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:76:65-76:69 -> tupleTest.fut:58:13-58:34 -> tupleSparse.fut:47:1-54:76",
                                   "Function return value does not match shape of type ",
                                   "matrix");
            memblock_unref(ctx, &res_mem_14041, "res_mem_14041");
            memblock_unref(ctx, &res_mem_14039, "res_mem_14039");
            memblock_unref(ctx, &res_mem_14037, "res_mem_14037");
            memblock_unref(ctx, &mem_14013, "mem_14013");
            memblock_unref(ctx, &mem_14010, "mem_14010");
            memblock_unref(ctx, &mem_13989, "mem_13989");
            memblock_unref(ctx, &mem_13986, "mem_13986");
            memblock_unref(ctx, &mem_13983, "mem_13983");
            memblock_unref(ctx, &mem_13972, "mem_13972");
            memblock_unref(ctx, &mem_13969, "mem_13969");
            memblock_unref(ctx, &mem_13964, "mem_13964");
            memblock_unref(ctx, &mem_13959, "mem_13959");
            memblock_unref(ctx, &mem_13956, "mem_13956");
            memblock_unref(ctx, &mem_13953, "mem_13953");
            memblock_unref(ctx, &mem_13862, "mem_13862");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        bool eq_x_y_13060 = 4 == x_13004;
        bool eq_x_zz_13061 = 4 == conc_tmp_13018;
        bool p_and_eq_x_y_13062 = cond_13036 && eq_x_y_13060;
        bool p_and_eq_x_y_13063 = cond_13035 && eq_x_zz_13061;
        bool dim_eq_13064 = p_and_eq_x_y_13062 || p_and_eq_x_y_13063;
        bool arrays_equal_13065;
        
        if (dim_eq_13064) {
            bool all_equal_13067;
            bool redout_13476 = 1;
            
            for (int32_t i_13477 = 0; i_13477 < sizze_13037; i_13477++) {
                int32_t x_13071 = *(int32_t *) &res_mem_14041.mem[i_13477 * 4];
                int32_t y_13072 = *(int32_t *) &mem_13689.mem[i_13477 * 4];
                bool res_13073 = x_13071 == y_13072;
                bool res_13070 = res_13073 && redout_13476;
                bool redout_tmp_14218 = res_13070;
                
                redout_13476 = redout_tmp_14218;
            }
            all_equal_13067 = redout_13476;
            arrays_equal_13065 = all_equal_13067;
        } else {
            arrays_equal_13065 = 0;
        }
        memblock_unref(ctx, &res_mem_14041, "res_mem_14041");
        
        bool res_13074;
        
        if (arrays_equal_13065) {
            bool arrays_equal_13075;
            
            if (dim_eq_13064) {
                bool all_equal_13077;
                bool redout_13478 = 1;
                
                for (int32_t i_13479 = 0; i_13479 < sizze_13037; i_13479++) {
                    int32_t x_13081 = *(int32_t *) &res_mem_14037.mem[i_13479 *
                                                                      4];
                    int32_t y_13082 = *(int32_t *) &mem_13953.mem[i_13479 * 4];
                    bool res_13083 = x_13081 == y_13082;
                    bool res_13080 = res_13083 && redout_13478;
                    bool redout_tmp_14219 = res_13080;
                    
                    redout_13478 = redout_tmp_14219;
                }
                all_equal_13077 = redout_13478;
                arrays_equal_13075 = all_equal_13077;
            } else {
                arrays_equal_13075 = 0;
            }
            
            bool arrays_equal_13084;
            
            if (dim_eq_13064) {
                bool all_equal_13086;
                bool redout_13480 = 1;
                
                for (int32_t i_13481 = 0; i_13481 < sizze_13037; i_13481++) {
                    int32_t x_13090 = *(int32_t *) &res_mem_14039.mem[i_13481 *
                                                                      4];
                    int32_t y_13091 = *(int32_t *) &mem_13956.mem[i_13481 * 4];
                    bool res_13092 = x_13090 == y_13091;
                    bool res_13089 = res_13092 && redout_13480;
                    bool redout_tmp_14220 = res_13089;
                    
                    redout_13480 = redout_tmp_14220;
                }
                all_equal_13086 = redout_13480;
                arrays_equal_13084 = all_equal_13086;
            } else {
                arrays_equal_13084 = 0;
            }
            
            bool eq_13093 = arrays_equal_13075 && arrays_equal_13084;
            
            res_13074 = eq_13093;
        } else {
            res_13074 = 0;
        }
        memblock_unref(ctx, &res_mem_14037, "res_mem_14037");
        memblock_unref(ctx, &res_mem_14039, "res_mem_14039");
        res_12974 = res_13074;
        memblock_unref(ctx, &res_mem_14041, "res_mem_14041");
        memblock_unref(ctx, &res_mem_14039, "res_mem_14039");
        memblock_unref(ctx, &res_mem_14037, "res_mem_14037");
        memblock_unref(ctx, &mem_14013, "mem_14013");
        memblock_unref(ctx, &mem_14010, "mem_14010");
        memblock_unref(ctx, &mem_13989, "mem_13989");
        memblock_unref(ctx, &mem_13986, "mem_13986");
        memblock_unref(ctx, &mem_13983, "mem_13983");
        memblock_unref(ctx, &mem_13972, "mem_13972");
        memblock_unref(ctx, &mem_13969, "mem_13969");
        memblock_unref(ctx, &mem_13964, "mem_13964");
        memblock_unref(ctx, &mem_13959, "mem_13959");
    } else {
        res_12974 = 0;
    }
    memblock_unref(ctx, &mem_13689, "mem_13689");
    memblock_unref(ctx, &mem_13953, "mem_13953");
    memblock_unref(ctx, &mem_13956, "mem_13956");
    
    bool cond_13094;
    
    if (res_12974) {
        struct memblock mem_14044;
        
        mem_14044.references = NULL;
        memblock_alloc(ctx, &mem_14044, 16, "mem_14044");
        
        struct memblock mem_14049;
        
        mem_14049.references = NULL;
        memblock_alloc(ctx, &mem_14049, 8, "mem_14049");
        for (int32_t i_13484 = 0; i_13484 < 2; i_13484++) {
            for (int32_t i_14222 = 0; i_14222 < 2; i_14222++) {
                *(int32_t *) &mem_14049.mem[i_14222 * 4] = i_13484;
            }
            memmove(mem_14044.mem + 2 * i_13484 * 4, mem_14049.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_14049, "mem_14049");
        
        struct memblock mem_14054;
        
        mem_14054.references = NULL;
        memblock_alloc(ctx, &mem_14054, 16, "mem_14054");
        
        struct memblock mem_14057;
        
        mem_14057.references = NULL;
        memblock_alloc(ctx, &mem_14057, 16, "mem_14057");
        
        int32_t discard_13494;
        int32_t scanacc_13488 = 0;
        
        for (int32_t i_13491 = 0; i_13491 < 4; i_13491++) {
            bool not_arg_13105 = i_13491 == 0;
            bool res_13106 = !not_arg_13105;
            int32_t part_res_13107;
            
            if (res_13106) {
                part_res_13107 = 0;
            } else {
                part_res_13107 = 1;
            }
            
            int32_t part_res_13108;
            
            if (res_13106) {
                part_res_13108 = 1;
            } else {
                part_res_13108 = 0;
            }
            
            int32_t zz_13103 = part_res_13108 + scanacc_13488;
            
            *(int32_t *) &mem_14054.mem[i_13491 * 4] = zz_13103;
            *(int32_t *) &mem_14057.mem[i_13491 * 4] = part_res_13107;
            
            int32_t scanacc_tmp_14223 = zz_13103;
            
            scanacc_13488 = scanacc_tmp_14223;
        }
        discard_13494 = scanacc_13488;
        
        int32_t last_offset_13109 = *(int32_t *) &mem_14054.mem[12];
        int64_t binop_x_14067 = sext_i32_i64(last_offset_13109);
        int64_t bytes_14066 = 4 * binop_x_14067;
        struct memblock mem_14068;
        
        mem_14068.references = NULL;
        memblock_alloc(ctx, &mem_14068, bytes_14066, "mem_14068");
        
        struct memblock mem_14071;
        
        mem_14071.references = NULL;
        memblock_alloc(ctx, &mem_14071, bytes_14066, "mem_14071");
        
        struct memblock mem_14074;
        
        mem_14074.references = NULL;
        memblock_alloc(ctx, &mem_14074, bytes_14066, "mem_14074");
        for (int32_t write_iter_13495 = 0; write_iter_13495 < 4;
             write_iter_13495++) {
            int32_t write_iv_13499 =
                    *(int32_t *) &mem_14057.mem[write_iter_13495 * 4];
            int32_t write_iv_13500 =
                    *(int32_t *) &mem_14054.mem[write_iter_13495 * 4];
            int32_t new_index_13655 = squot32(write_iter_13495, 2);
            int32_t binop_y_13657 = 2 * new_index_13655;
            int32_t new_index_13658 = write_iter_13495 - binop_y_13657;
            int32_t write_iv_13501 =
                    *(int32_t *) &mem_14044.mem[(new_index_13655 * 2 +
                                                 new_index_13658) * 4];
            bool is_this_one_13121 = write_iv_13499 == 0;
            int32_t this_offset_13122 = -1 + write_iv_13500;
            int32_t total_res_13123;
            
            if (is_this_one_13121) {
                total_res_13123 = this_offset_13122;
            } else {
                total_res_13123 = -1;
            }
            
            bool less_than_zzero_13504 = slt32(total_res_13123, 0);
            bool greater_than_sizze_13505 = sle32(last_offset_13109,
                                                  total_res_13123);
            bool outside_bounds_dim_13506 = less_than_zzero_13504 ||
                 greater_than_sizze_13505;
            
            if (!outside_bounds_dim_13506) {
                *(int32_t *) &mem_14068.mem[total_res_13123 * 4] =
                    write_iv_13501;
            }
            if (!outside_bounds_dim_13506) {
                *(int32_t *) &mem_14071.mem[total_res_13123 * 4] =
                    new_index_13658;
            }
            if (!outside_bounds_dim_13506) {
                *(int32_t *) &mem_14074.mem[total_res_13123 * 4] =
                    write_iter_13495;
            }
        }
        memblock_unref(ctx, &mem_14044, "mem_14044");
        memblock_unref(ctx, &mem_14054, "mem_14054");
        memblock_unref(ctx, &mem_14057, "mem_14057");
        
        int32_t x_13124 = abs(last_offset_13109);
        bool empty_slice_13125 = x_13124 == 0;
        int32_t m_13126 = x_13124 - 1;
        bool zzero_leq_i_p_m_t_s_13127 = sle32(0, m_13126);
        bool i_p_m_t_s_leq_w_13128 = slt32(m_13126, last_offset_13109);
        bool y_13129 = zzero_leq_i_p_m_t_s_13127 && i_p_m_t_s_leq_w_13128;
        bool ok_or_empty_13130 = empty_slice_13125 || y_13129;
        bool index_certs_13131;
        
        if (!ok_or_empty_13130) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:77:11-77:15 -> tupleTest.fut:64:13-64:29 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_13109,
                                   "] out of bounds for array of shape [",
                                   last_offset_13109, "].");
            memblock_unref(ctx, &mem_14074, "mem_14074");
            memblock_unref(ctx, &mem_14071, "mem_14071");
            memblock_unref(ctx, &mem_14068, "mem_14068");
            memblock_unref(ctx, &mem_14057, "mem_14057");
            memblock_unref(ctx, &mem_14054, "mem_14054");
            memblock_unref(ctx, &mem_14049, "mem_14049");
            memblock_unref(ctx, &mem_14044, "mem_14044");
            memblock_unref(ctx, &mem_13956, "mem_13956");
            memblock_unref(ctx, &mem_13953, "mem_13953");
            memblock_unref(ctx, &mem_13862, "mem_13862");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        int32_t res_13135;
        int32_t redout_13522 = x_13124;
        
        for (int32_t i_13523 = 0; i_13523 < x_13124; i_13523++) {
            int32_t x_13139 = *(int32_t *) &mem_14068.mem[i_13523 * 4];
            int32_t x_13140 = *(int32_t *) &mem_14071.mem[i_13523 * 4];
            bool cond_13142 = x_13139 == 2;
            bool cond_13143 = x_13140 == 3;
            bool eq_13144 = cond_13142 && cond_13143;
            int32_t res_13145;
            
            if (eq_13144) {
                res_13145 = i_13523;
            } else {
                res_13145 = x_13124;
            }
            
            int32_t res_13138 = smin32(res_13145, redout_13522);
            int32_t redout_tmp_14229 = res_13138;
            
            redout_13522 = redout_tmp_14229;
        }
        res_13135 = redout_13522;
        memblock_unref(ctx, &mem_14068, "mem_14068");
        memblock_unref(ctx, &mem_14071, "mem_14071");
        
        bool cond_13146 = res_13135 == x_13124;
        int32_t res_13147;
        
        if (cond_13146) {
            res_13147 = -1;
        } else {
            res_13147 = res_13135;
        }
        
        bool eq_x_zz_13148 = -1 == res_13135;
        bool not_p_13149 = !cond_13146;
        bool p_and_eq_x_y_13150 = eq_x_zz_13148 && not_p_13149;
        bool cond_13151 = cond_13146 || p_and_eq_x_y_13150;
        int32_t res_13152;
        
        if (cond_13151) {
            res_13152 = 0;
        } else {
            int32_t res_13153 = *(int32_t *) &mem_14074.mem[res_13147 * 4];
            
            res_13152 = res_13153;
        }
        memblock_unref(ctx, &mem_14074, "mem_14074");
        
        bool res_13154 = res_13152 == 0;
        
        cond_13094 = res_13154;
        memblock_unref(ctx, &mem_14074, "mem_14074");
        memblock_unref(ctx, &mem_14071, "mem_14071");
        memblock_unref(ctx, &mem_14068, "mem_14068");
        memblock_unref(ctx, &mem_14057, "mem_14057");
        memblock_unref(ctx, &mem_14054, "mem_14054");
        memblock_unref(ctx, &mem_14049, "mem_14049");
        memblock_unref(ctx, &mem_14044, "mem_14044");
    } else {
        cond_13094 = 0;
    }
    
    bool res_13155;
    
    if (cond_13094) {
        struct memblock mem_14095;
        
        mem_14095.references = NULL;
        memblock_alloc(ctx, &mem_14095, 16, "mem_14095");
        
        struct memblock mem_14100;
        
        mem_14100.references = NULL;
        memblock_alloc(ctx, &mem_14100, 8, "mem_14100");
        for (int32_t i_13526 = 0; i_13526 < 2; i_13526++) {
            for (int32_t i_14231 = 0; i_14231 < 2; i_14231++) {
                *(int32_t *) &mem_14100.mem[i_14231 * 4] = i_13526;
            }
            memmove(mem_14095.mem + 2 * i_13526 * 4, mem_14100.mem + 0, 2 *
                    sizeof(int32_t));
        }
        memblock_unref(ctx, &mem_14100, "mem_14100");
        
        struct memblock mem_14105;
        
        mem_14105.references = NULL;
        memblock_alloc(ctx, &mem_14105, 16, "mem_14105");
        
        struct memblock mem_14108;
        
        mem_14108.references = NULL;
        memblock_alloc(ctx, &mem_14108, 16, "mem_14108");
        
        int32_t discard_13536;
        int32_t scanacc_13530 = 0;
        
        for (int32_t i_13533 = 0; i_13533 < 4; i_13533++) {
            bool not_arg_13166 = i_13533 == 0;
            bool res_13167 = !not_arg_13166;
            int32_t part_res_13168;
            
            if (res_13167) {
                part_res_13168 = 0;
            } else {
                part_res_13168 = 1;
            }
            
            int32_t part_res_13169;
            
            if (res_13167) {
                part_res_13169 = 1;
            } else {
                part_res_13169 = 0;
            }
            
            int32_t zz_13164 = part_res_13169 + scanacc_13530;
            
            *(int32_t *) &mem_14105.mem[i_13533 * 4] = zz_13164;
            *(int32_t *) &mem_14108.mem[i_13533 * 4] = part_res_13168;
            
            int32_t scanacc_tmp_14232 = zz_13164;
            
            scanacc_13530 = scanacc_tmp_14232;
        }
        discard_13536 = scanacc_13530;
        
        int32_t last_offset_13170 = *(int32_t *) &mem_14105.mem[12];
        int64_t binop_x_14118 = sext_i32_i64(last_offset_13170);
        int64_t bytes_14117 = 4 * binop_x_14118;
        struct memblock mem_14119;
        
        mem_14119.references = NULL;
        memblock_alloc(ctx, &mem_14119, bytes_14117, "mem_14119");
        
        struct memblock mem_14122;
        
        mem_14122.references = NULL;
        memblock_alloc(ctx, &mem_14122, bytes_14117, "mem_14122");
        
        struct memblock mem_14125;
        
        mem_14125.references = NULL;
        memblock_alloc(ctx, &mem_14125, bytes_14117, "mem_14125");
        for (int32_t write_iter_13537 = 0; write_iter_13537 < 4;
             write_iter_13537++) {
            int32_t write_iv_13541 =
                    *(int32_t *) &mem_14108.mem[write_iter_13537 * 4];
            int32_t write_iv_13542 =
                    *(int32_t *) &mem_14105.mem[write_iter_13537 * 4];
            int32_t new_index_13672 = squot32(write_iter_13537, 2);
            int32_t binop_y_13674 = 2 * new_index_13672;
            int32_t new_index_13675 = write_iter_13537 - binop_y_13674;
            int32_t write_iv_13543 =
                    *(int32_t *) &mem_14095.mem[(new_index_13672 * 2 +
                                                 new_index_13675) * 4];
            bool is_this_one_13182 = write_iv_13541 == 0;
            int32_t this_offset_13183 = -1 + write_iv_13542;
            int32_t total_res_13184;
            
            if (is_this_one_13182) {
                total_res_13184 = this_offset_13183;
            } else {
                total_res_13184 = -1;
            }
            
            bool less_than_zzero_13546 = slt32(total_res_13184, 0);
            bool greater_than_sizze_13547 = sle32(last_offset_13170,
                                                  total_res_13184);
            bool outside_bounds_dim_13548 = less_than_zzero_13546 ||
                 greater_than_sizze_13547;
            
            if (!outside_bounds_dim_13548) {
                *(int32_t *) &mem_14119.mem[total_res_13184 * 4] =
                    write_iv_13543;
            }
            if (!outside_bounds_dim_13548) {
                *(int32_t *) &mem_14122.mem[total_res_13184 * 4] =
                    new_index_13675;
            }
            if (!outside_bounds_dim_13548) {
                *(int32_t *) &mem_14125.mem[total_res_13184 * 4] =
                    write_iter_13537;
            }
        }
        memblock_unref(ctx, &mem_14095, "mem_14095");
        memblock_unref(ctx, &mem_14105, "mem_14105");
        memblock_unref(ctx, &mem_14108, "mem_14108");
        
        int32_t x_13185 = abs(last_offset_13170);
        bool empty_slice_13186 = x_13185 == 0;
        int32_t m_13187 = x_13185 - 1;
        bool zzero_leq_i_p_m_t_s_13188 = sle32(0, m_13187);
        bool i_p_m_t_s_leq_w_13189 = slt32(m_13187, last_offset_13170);
        bool y_13190 = zzero_leq_i_p_m_t_s_13188 && i_p_m_t_s_leq_w_13189;
        bool ok_or_empty_13191 = empty_slice_13186 || y_13190;
        bool index_certs_13192;
        
        if (!ok_or_empty_13191) {
            ctx->error = msgprintf("Error at %s:\n%s%s%s%d%s%d%s\n",
                                   "tupleTest.fut:75:1-77:24 -> tupleTest.fut:77:20-77:24 -> tupleTest.fut:71:13-71:29 -> tupleSparse.fut:23:18-23:61 -> /futlib/soacs.fut:135:6-135:16",
                                   "Index [", "", ":", last_offset_13170,
                                   "] out of bounds for array of shape [",
                                   last_offset_13170, "].");
            memblock_unref(ctx, &mem_14125, "mem_14125");
            memblock_unref(ctx, &mem_14122, "mem_14122");
            memblock_unref(ctx, &mem_14119, "mem_14119");
            memblock_unref(ctx, &mem_14108, "mem_14108");
            memblock_unref(ctx, &mem_14105, "mem_14105");
            memblock_unref(ctx, &mem_14100, "mem_14100");
            memblock_unref(ctx, &mem_14095, "mem_14095");
            memblock_unref(ctx, &mem_13956, "mem_13956");
            memblock_unref(ctx, &mem_13953, "mem_13953");
            memblock_unref(ctx, &mem_13862, "mem_13862");
            memblock_unref(ctx, &mem_13748, "mem_13748");
            memblock_unref(ctx, &mem_13745, "mem_13745");
            memblock_unref(ctx, &mem_13742, "mem_13742");
            memblock_unref(ctx, &mem_13739, "mem_13739");
            memblock_unref(ctx, &mem_13736, "mem_13736");
            memblock_unref(ctx, &mem_13715, "mem_13715");
            memblock_unref(ctx, &mem_13712, "mem_13712");
            memblock_unref(ctx, &mem_13709, "mem_13709");
            memblock_unref(ctx, &mem_13702, "mem_13702");
            memblock_unref(ctx, &mem_13697, "mem_13697");
            memblock_unref(ctx, &mem_13692, "mem_13692");
            memblock_unref(ctx, &mem_13689, "mem_13689");
            return 1;
        }
        
        int32_t res_13196;
        int32_t redout_13564 = x_13185;
        
        for (int32_t i_13565 = 0; i_13565 < x_13185; i_13565++) {
            int32_t x_13200 = *(int32_t *) &mem_14119.mem[i_13565 * 4];
            int32_t x_13201 = *(int32_t *) &mem_14122.mem[i_13565 * 4];
            bool cond_13203 = x_13200 == 0;
            bool cond_13204 = x_13201 == 1;
            bool eq_13205 = cond_13203 && cond_13204;
            int32_t res_13206;
            
            if (eq_13205) {
                res_13206 = i_13565;
            } else {
                res_13206 = x_13185;
            }
            
            int32_t res_13199 = smin32(res_13206, redout_13564);
            int32_t redout_tmp_14238 = res_13199;
            
            redout_13564 = redout_tmp_14238;
        }
        res_13196 = redout_13564;
        memblock_unref(ctx, &mem_14119, "mem_14119");
        memblock_unref(ctx, &mem_14122, "mem_14122");
        
        bool cond_13207 = res_13196 == x_13185;
        int32_t res_13208;
        
        if (cond_13207) {
            res_13208 = -1;
        } else {
            res_13208 = res_13196;
        }
        
        bool eq_x_zz_13209 = -1 == res_13196;
        bool not_p_13210 = !cond_13207;
        bool p_and_eq_x_y_13211 = eq_x_zz_13209 && not_p_13210;
        bool cond_13212 = cond_13207 || p_and_eq_x_y_13211;
        int32_t res_13213;
        
        if (cond_13212) {
            res_13213 = 0;
        } else {
            int32_t res_13214 = *(int32_t *) &mem_14125.mem[res_13208 * 4];
            
            res_13213 = res_13214;
        }
        memblock_unref(ctx, &mem_14125, "mem_14125");
        
        bool res_13215 = res_13213 == 1;
        
        res_13155 = res_13215;
        memblock_unref(ctx, &mem_14125, "mem_14125");
        memblock_unref(ctx, &mem_14122, "mem_14122");
        memblock_unref(ctx, &mem_14119, "mem_14119");
        memblock_unref(ctx, &mem_14108, "mem_14108");
        memblock_unref(ctx, &mem_14105, "mem_14105");
        memblock_unref(ctx, &mem_14100, "mem_14100");
        memblock_unref(ctx, &mem_14095, "mem_14095");
    } else {
        res_13155 = 0;
    }
    scalar_out_14144 = res_13155;
    *out_scalar_out_14239 = scalar_out_14144;
    memblock_unref(ctx, &mem_13956, "mem_13956");
    memblock_unref(ctx, &mem_13953, "mem_13953");
    memblock_unref(ctx, &mem_13862, "mem_13862");
    memblock_unref(ctx, &mem_13748, "mem_13748");
    memblock_unref(ctx, &mem_13745, "mem_13745");
    memblock_unref(ctx, &mem_13742, "mem_13742");
    memblock_unref(ctx, &mem_13739, "mem_13739");
    memblock_unref(ctx, &mem_13736, "mem_13736");
    memblock_unref(ctx, &mem_13715, "mem_13715");
    memblock_unref(ctx, &mem_13712, "mem_13712");
    memblock_unref(ctx, &mem_13709, "mem_13709");
    memblock_unref(ctx, &mem_13702, "mem_13702");
    memblock_unref(ctx, &mem_13697, "mem_13697");
    memblock_unref(ctx, &mem_13692, "mem_13692");
    memblock_unref(ctx, &mem_13689, "mem_13689");
    return 0;
}
int futhark_entry_main(struct futhark_context *ctx, bool *out0)
{
    bool scalar_out_14144;
    
    lock_lock(&ctx->lock);
    
    int ret = futrts_main(ctx, &scalar_out_14144);
    
    if (ret == 0) {
        *out0 = scalar_out_14144;
    }
    lock_unlock(&ctx->lock);
    return ret;
}
