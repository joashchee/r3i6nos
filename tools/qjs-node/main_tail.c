/* The main() of a QuickJS command-line tool. build.sh appends this to what qjsc generated, in place of qjsc's own main, which exits 0 with no message when
 * the program throws (a promise rejection nobody reads). Here an uncaught error is printed to stderr and the exit status is 1, as in Node. */
static void qjs_node_rejection(JSContext *ctx, JSValueConst promise, JSValueConst reason, JS_BOOL is_handled, void *opaque)
{
  (void)promise; (void)opaque;
  if (is_handled)
    return;
  fflush(stdout);
  JS_Throw(ctx, JS_DupValue(ctx, reason));
  js_std_dump_error(ctx);
  fflush(stderr);
  exit(1);
}

int main(int argc, char **argv)
{
  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  JS_SetMaxStackSize(rt, 4 * 1024 * 1024); /* qjsc's default of 256 KB is far below Node's, and deep recursion in a recipe would overflow it */
  js_std_set_worker_new_context_func(JS_NewCustomContext);
  js_std_init_handlers(rt);
  JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL);
  JS_SetHostPromiseRejectionTracker(rt, qjs_node_rejection, NULL);
  JSContext *ctx = JS_NewCustomContext(rt);
  if (!ctx) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  js_std_add_helpers(ctx, argc, argv);
  js_std_eval_binary(ctx, qjsc_main, qjsc_main_size, 0);
  js_std_loop(ctx);
  fflush(stdout);
  fflush(stderr);
  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 0;
}
