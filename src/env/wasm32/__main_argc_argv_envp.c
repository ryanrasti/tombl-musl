#include <features.h>

/*
 * main(argc, argv, envp): stock clang leaves it named main (only
 * main(void) and main(argc, argv) get __main_void and __main_argc_argv);
 * tombl's LLVM names it __main_argc_argv_envp, which then wins.
 */
weak int main(int, char **, char **);

weak int __main_argc_argv_envp(int argc, char **argv, char **envp)
{
	return main(argc, argv, envp);
}
