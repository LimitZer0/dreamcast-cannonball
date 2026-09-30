/* EasyCompile: entry point for the WebAssembly recording tool. Runs the
   host program's main() (built with RENDER_AUDIO) with the config file the
   page wrote to /w/config.xml; CB_RENDER/CB_CMD/CB_OUT come from ENV. */
extern int __main_argc_argv(int, char**);
int cb_run(void)
{
    static char a0[] = "cannonball", a1[] = "-cfgfile", a2[] = "/w/config.xml";
    char* v[] = { a0, a1, a2, 0 };
    return __main_argc_argv(3, v);
}
