#include <stdio.h>
#include <string.h>
#include "ldaptypes.h"
#include "debug.h"

struct type_entry {
	const char *rng_name;
	const char *ldap_equality;
	const char *ldap_syntax;
};


struct type_entry type_table[] = {
{ "boolean",	"booleanMatch",		"1.3.6.1.4.1.1466.115.121.1.7" },
{ "integer",	"integerMatch",		"1.3.6.1.4.1.1466.115.121.1.27" },
{ "positiveInteger",	"integerMatch",	"1.3.6.1.4.1.1466.115.121.1.27" },
{ "nonNegativeInteger",	"integerMatch",	"1.3.6.1.4.1.1466.115.121.1.27" },
{ "string",	"caseExactIA5Match",	"1.3.6.1.4.1.1466.115.121.1.26" },
{ "ID",		"caseExactIA5Match",	"1.3.6.1.4.1.1466.115.121.1.26" },
{ NULL,		"caseExactIA5Match",	"1.3.6.1.4.1.1466.115.121.1.26" } };


void
find_ldap_type_info(const char *name,
		    char **equality,
		    char **syntax)
{
	int x;

	for (x = 0; type_table[x].rng_name != NULL; x++) 
		if (!strcasecmp(name, type_table[x].rng_name))
			break;
	dbg_printf("%s @ index %d\n", name, x);

	*equality = (char *)type_table[x].ldap_equality;
	*syntax = (char *)type_table[x].ldap_syntax;
}


#ifdef STANDALONE
#include <stdio.h>
int
main(int argc, char **argv)
{
	char *eq, *syn;

	find_ldap_type_info(argv[1], &eq, &syn);

	printf("EQUALITY %s\nSYNTAX %s\n", eq, syn);

	return 0;
}
#endif
