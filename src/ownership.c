#include "mosaic_internal.h"
#include <stdlib.h>

struct mosaic_lease { mosaic_fn_obj *fn; };

mosaic_lease *mosaic_lease_acquire(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  mosaic_fn_obj *fn = ws_find(rt, fn_id);
  if (!fn) fn = mosaic_fn_materialize(rt, fn_id);
  if (!fn) return NULL;
  fn->refs++;
  mosaic_lease *l = malloc(sizeof *l);
  if (!l) { fn->refs--; rt->last_err = MOSAIC_ERR_NOMEM; return NULL; }
  l->fn = fn;
  return l;
}

void mosaic_lease_release(mosaic_lease *l) {
  if (!l) return;
  if (l->fn && l->fn->refs) l->fn->refs--;
  free(l);
}
