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
#include <math.h>

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

int             clearance = 2;
int             dive = 20;
int             park = 5;
double          lastx = 0,
                lasty = 0,
                lastz = 0;
double
z(double x, double y)
{
   //Get z axis at a point
   int             try = 0;
   while (1)
   {
      double          z = lastz;
      tx("G1 Z%lfF1000\n", lastz + (lastx == x && lasty == y ? 0.25 : clearance));
      tx("G1 X%lfY%lfF1000\n", x, y);
      tx("G38.2 Z%lf F%d\n", lastz - dive, try++ ? 5 : 100);
      int             n = 0;
      struct pollfd   fds = {.fd = p,.events = POLLIN};
      char            buf[1000];
      while (poll(&fds, 1, 1000) > 0)
      {
         int             l = read(p, buf + n, sizeof(buf) - n - 1);
         if (l <= 0)
            errx(1, "read");
         n += l;
         buf[n] = 0;
         if (n >= sizeof(buf) - 1)
            n = 0;              /* too long */
         else
         {
            for (l = 0; l < n && buf[l] != '\n' && buf[l] != '\r'; l++);
            if (l <= n)
            {
               if (l < n)
                  buf[l++] = 0;
               if (debug)
                  fprintf(stderr, "Rx: %s\n", buf);
               if (strstr(buf, "\"prb\""))
                  break;
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
      if (debug)
         fprintf(stderr, "z=%lf\n", z);
      dive = 1;
      lastx = x;
      lasty = y;
      if (round(z * 10) == round(lastz * 10))
      {
         lastz = z;
         break;
      }
      lastz = z;
   }
   return lastz;
}

int
main(int argc, const char *argv[])
{
   double          width = 0;
   double          height = 0;
   int             point = 0;
   const char     *port = NULL;
   {
      poptContext     optCon;
      const struct poptOption optionsTable[] = {
         {"port", 'p', POPT_ARG_STRING, &port, 0, "Serial port", "/dev/cu.usb..."},
         {"width", 'w', POPT_ARG_DOUBLE, &width, 0, "Width", "mm"},
         {"height", 'h', POPT_ARG_DOUBLE, &height, 0, "Height", "mm"},
         {"point", 0, POPT_ARG_NONE, &point, 0, "Width/height in pt not mm"},
         {"clearance", 'c', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &clearance, 0, "Clearance", "mm"},
         {"dive", 'd', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &dive, 0, "Depth to go", "mm"},
         {"park", 'P', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &park, 0, "Park at", "mm"},
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
   if (point)
   {
      width *= 25.4 / 72;
      height *= 25.4 / 72;
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
   tx("G28.3 X0Y0Z0\n");        /* origin */
   tx("G1 Z%lfF1000\n", clearance);
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
   tx("G1 Z%lfF1000\n", a + clearance);
   tx("G1 X0Y0F1000\n");        /* home */
   tx("G28.3 X0Y0Z%d\n", clearance);    /* origin */
   tx("G1 Z%dF1000\n", park);
   close(p);
   printf("%lf %lf %lf %lf %lf\n", width, height, b - a, c - a, d - a);
   return 0;
}
