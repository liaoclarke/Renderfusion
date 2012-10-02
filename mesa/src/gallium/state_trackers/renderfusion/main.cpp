#include <stdio.h>
#include <pipe_loader.h>
#include <pipe/p_context.h>
#include <pipe/p_screen.h>
#include "pipe_loader_priv.h"
int main() {
  int n = pipe_loader_probe(NULL, 0);

  printf("n: %d\n", n);

  pipe_loader_device * ldev;

  pipe_loader_probe(&ldev, n);

  fprintf(stderr, "ldev: %p ops = %x \n", ldev, ldev->ops);
  fprintf(stderr, "create screen func = %x\n", ldev->ops->create_screen);
 
  pipe_screen *pipe = pipe_loader_create_screen(ldev, "/data");

  printf("pipe: %p\n", pipe);

  pipe_context *ctx = pipe->context_create(pipe, NULL);

  printf("ctx: %p\n", ctx);



}
