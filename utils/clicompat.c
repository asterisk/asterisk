/*
 * Stubs for some cli functions used by the test routines.
 * $Revision$
 */
void ast_cli(int fd, const char *fmt, ...);
void ast_cli(int fd, const char *fmt, ...)
{
}

struct ast_cli_entry;

int ast_register_atexit(void (*func)(void));
int ast_register_atexit(void (*func)(void))
{
	return 0;
}

int ast_register_cleanup(void (*func)(void));
int ast_register_cleanup(void (*func)(void))
{
	return 0;
}

int ast_cli_register_multiple(struct ast_cli_entry *e, int len);
int ast_cli_register_multiple(struct ast_cli_entry *e, int len)
{
	return 0;
}
int ast_cli_unregister_multiple(struct ast_cli_entry *e, int len);
int ast_cli_unregister_multiple(struct ast_cli_entry *e, int len)
{
	return 0;
}
