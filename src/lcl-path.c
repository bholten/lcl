#include <stdlib.h>
#include <string.h>

#include "lcl-path.h"
#include "str-compat.h"

char *lcl_path_clean(const char *path) {
  size_t len = strlen(path);
  char *out = (char *)malloc(len + 2);
  size_t r = 0;
  size_t w = 0;
  size_t floor = 0;
  int rooted = (path[0] == '/');

  if (!out) {
    return NULL;
  }

  if (rooted) {
    out[w++] = '/';
    floor = 1;
    r = 1;
  }

  while (r < len) {
    if (path[r] == '/') {
      r++;
    } else if (path[r] == '.' && (r + 1 == len || path[r + 1] == '/')) {
      r++;
    } else if (path[r] == '.' && r + 1 < len && path[r + 1] == '.' &&
               (r + 2 == len || path[r + 2] == '/')) {
      r += 2;

      if (w > floor) {
        while (w > floor && out[w - 1] != '/') {
          w--;
        }

        if (w > floor) {
          w--;
        }
      } else if (!rooted) {
        if (w > 0) {
          out[w++] = '/';
        }

        out[w++] = '.';
        out[w++] = '.';
        floor = w;
      }
    } else {
      if (w > 0 && out[w - 1] != '/') {
        out[w++] = '/';
      }

      while (r < len && path[r] != '/') {
        out[w++] = path[r++];
      }
    }
  }

  if (w == 0) {
    out[w++] = '.';
  }

  out[w] = '\0';
  return out;
}

char *lcl_path_join(const char *base, const char *rel) {
  char *raw;
  char *cleaned;
  size_t blen;

  if (rel[0] == '/' || base[0] == '\0') {
    return lcl_path_clean(rel);
  }

  blen = strlen(base);
  raw = (char *)malloc(blen + 1 + strlen(rel) + 1);

  if (!raw) {
    return NULL;
  }

  memcpy(raw, base, blen);
  raw[blen] = '/';
  strcpy(raw + blen + 1, rel);

  cleaned = lcl_path_clean(raw);
  free(raw);
  return cleaned;
}

char *lcl_path_dirname(const char *path) {
  const char *slash = strrchr(path, '/');

  if (!slash) {
    return strdup(".");
  }

  if (slash == path) {
    return strdup("/");
  }

  return strndup(path, (size_t)(slash - path));
}
