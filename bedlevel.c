// Simple bed level

#include <popt.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>

int             debug = 0;

int             p = 0;          /* port */
void
tx(const char *fmt,...)
{
   char           *buf = NULL;
   va_list         ap;
   va_start(ap, fmt);
   if (vasprintf(&buf, fmt, ap) < 0)
      errx(1, "malloc");
   va_end(ap);
   int             n = write(p, buf, strlen(buf));
   if (n < p)
      err(1, "Write");
   if (debug)
      fprintf(stderr, "Tx: %s", buf);
   free(buf);
}

int             clearance = 3;
int             dive = 20;
double
z(int x, int y)
{
   //Get z axis at a point
   double          z = 0;
   tx("G1 X%dY%dF1000\n", x, y);
   tx("G91\n");                 /* rel */
   tx("G38.2 Z-%d F20\n", dive);
   tx("G90\n");                 /* abs */
   char            buf[1000];
   struct pollfd   fds = {.fd = p,.events = POLLIN};
   int             n = 0;
   while (poll(&fds, 1, 1000) > 0)
   {
      int             l = read(p, buf + n, sizeof(buf) - n - 1);
      if (l <= 0)
         errx(1, "read");
      n += l;
      buf[n] = 0;
      if (n >= sizeof(buf) - 1)
         n = 0;                 /* too long */
      else
      {
         for (l = 0; l < n && buf[l] != '\n' && buf[l] != '\r'; l++);
         if (l <= n)
         {
            if (l < n)
               buf[l++] = 0;
            if (debug)
               fprintf(stderr, "Rx: %s\n", buf);
            char           *zp = strstr(buf, "\"posz\":");
            if (zp)
               z = strtod(zp + 7, NULL);
            while (l < n && (buf[l] == '\r' || buf[l] == '\n'))
               l++;
            if (l < n)
               memmove(buf, buf + l, n - l);
            n -= l;
         }
      }
   }
   tx("G1 Z%lf F1000\n", z + clearance);
   return z;
}

int
main(int argc, const char *argv[])
{
   int             width = 0;
   int             height = 0;
   const char     *port = NULL;
   {
      poptContext     optCon;
      const struct poptOption optionsTable[] = {
         {"port", 'p', POPT_ARG_STRING, &port, 0, "Serial port", "/dev/cu.usb..."},
         {"width", 'w', POPT_ARG_INT, &width, 0, "Width", "mm"},
         {"height", 'h', POPT_ARG_INT, &height, 0, "Height", "mm"},
         {"clearance", 'c', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &clearance, 0, "Clearance", "mm"},
         {"dive", 'd', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &dive, 0, "Depth to go", "mm"},
         {"debug", 'V', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };
      optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
      int             c;
      if ((c = poptGetNextOpt(optCon)) < -1)
         errx(1, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
      if (!port && poptPeekArg(optCon))
         port = poptGetArg(optCon);
      if (!port || poptPeekArg(optCon))
      {
         poptPrintUsage(optCon, stderr, 0);
         return -1;
      }
      poptFreeContext(optCon);
   }
   p = open(port, O_RDWR | O_EXLOCK);
   if (p < 0)
      err(1, "Cannot open %s", port);
   struct termios  t;
   if (tcgetattr(p, &t))
      err(1, "tcgetattr");
   sleep(1);
   t.c_ispeed = B115200;
   t.c_ospeed = B115200;
   t.c_cflag |= CCTS_OFLOW;
   if (tcsetattr(p, TCSANOW, &t))
      err(1, "tcsetattr");
   double          a = 0,
                   b = 0,
                   c = 0,
                   d = 0;
   tx("G90\n");                 /* absolute */
   tx("G28.3 Z0Y0Z0\n");        /* origin */
   tx("G1 Z1F1000\n");          /* just in case */
   a = z(0, 0);
   if (width)
      b = z(width, 0);
   else
      b = a;
   if (width && height)
      c = z(width, height);
   else
      c = b;
   if (height)
      d = z(0, height);
   else
      d = c;
   tx("G1 X0Y0F1000\n");        /* home */
   tx("G28.3 Z0Y0Z%d\n", clearance);    /* origin */
   close(p);
   printf("%lf %lf %lf %lf\n", a + clearance, b + clearance, c + clearance, d + clearance);
   return 0;
}
