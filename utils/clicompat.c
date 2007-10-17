void ast_cli(int fd, const char *fmt, ...);
void ast_cli(int fd, const char *fmt, ...)
{
}

struct ast_cli_entry {
       char * const cmda; /* just something to satisfy compile & link; will never be used */
};

int ast_cli_register_multiple(struct ast_cli_entry *e, int len);
int ast_cli_register_multiple(struct ast_cli_entry *e, int len)
{
 return 0;
}


