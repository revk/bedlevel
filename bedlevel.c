// Simple bed level

#include <popt.h>
#include <stdio.h>
#include <err.h>

int 
main(int argc, const char *argv[])
{
	int		debug = 0;
	const char     *port = NULL;
	{
		//POPT
			poptContext optCon;
		//context for parsing
			command - line options
				const struct poptOption optionsTable[] = {
				{"port", 'p', POPT_ARG_STRING, &port, 0, "Serial port", "/dev/cu.usb..."},
				{"debug", 'V', POPT_ARG_NONE, &debug, 0, "Debug"},
				POPT_AUTOHELP {}
			};
		optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
		int		c;
		if ((c = poptGetNextOpt(optCon)) < -1)
			errx(1, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
		if (!port || poptPeekArg(optCon)) {
			poptPrintUsage(optCon, stderr, 0);
			return -1;
		}
		poptFreeContext(optCon);
	}

	return 0;
}
