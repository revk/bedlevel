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
int             quiet = 0;

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

double          tolerance = 0.01;
//How close to allow
int             clearance = 2;
int             dive = 20;
int             park = 5;
int             fast = 2000;
double          lastx = 0,
                lasty = 0,
                lastz = 0;

void
waitrx(void)
{
   while (1)
   {
      struct pollfd   fds = {.fd = p,.events = POLLIN};
      if (poll(&fds, 1, 500) <= 0)
         break;
      char            buf[1000];
      int             l = read(p, buf, sizeof(buf) - 1);
      if (l <= 0)
         errx(1, "read");
      buf[l] = 0;
      if (debug)
         fprintf(stderr, "%.*s", l, buf);
      if (strstr(buf, "{\"qr\":32}"))
         break;
   }
}

double
z(double x, double y)
{
   //Get z axis at a point
   int             try = 0;
   while (try++ < 20)
   {
      if (debug)
         fprintf(stderr, "Try %d\n", try);
      double          z = lastz;
      double          d = (try == 1 ? 0.5 : 0);
      /* First step may cut in, so offset slightly for more accurate measure next */
      tx("G1 Z%lfF%d\n", lastz + (try == 1 ? clearance : try == 2 ? 0.25 : 0.1), fast);
      waitrx();
      tx("G1 X%lfY%lfF%d\n", x + d, y, fast);
      waitrx();
      tx("G38.2 Z%lf F%d\n", lastz - dive, try == 1 ? 100 : try == 2 ? 10 : 2);
      int             n = 0;
      char            buf[1000];
      char            done = 0;
      while (!done)
      {
         struct pollfd   fds = {.fd = p,.events = POLLIN};
         if (poll(&fds, 1, 1000) <= 0)
            break;
         int             l = read(p, buf + n, sizeof(buf) - n - 1);
         if (l <= 0)
            errx(1, "read");
         n += l;
         buf[n] = 0;
         if (n >= sizeof(buf) - 1)
         {
            warnx("Buffer overrun");
            n = 0;              /* too long */
         } else
         {
            while (1)
            {
               for (l = 0; l < n && buf[l] != '\n' && buf[l] != '\r'; l++);
               if (l < n)
               {
                  buf[l++] = 0;
                  if (debug)
                     fprintf(stderr, "Rx: %s\n", buf);
                  if (strstr(buf, "\"prb\""))
                     done = 1;
                  char           *zp = strstr(buf, "\"posz\":");
                  if (zp)
                     z = strtod(zp + 7, NULL);
                  while (l < n && (buf[l] == '\r' || buf[l] == '\n'))
                     l++;
                  if (l < n)
                     memmove(buf, buf + l, n - l);
                  n -= l;
               } else
                  break;
            }
         }
      }
      if (debug)
         fprintf(stderr, "z=%lf (%.3f)\n", z, z - lastz);
      dive = 1;
      lastx = x;
      lasty = y;
      if (try > 2 && fabs(z - lastz) < tolerance)
      {
         lastz = z;
         break;
      }
      lastz = z;
   }
   if (!quiet)
      fprintf(stderr, "%5.1f/%5.1f %.3f\n", x, y, lastz);
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
         {"tolerance", 't', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &tolerance, 0, "Tolerance", "mm"},
         {"point", 0, POPT_ARG_NONE, &point, 0, "Width/height in pt not mm"},
         {"clearance", 'c', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &clearance, 0, "Clearance", "mm"},
         {"dive", 'd', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &dive, 0, "Depth to go", "mm"},
         {"park", 'P', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &park, 0, "Park at", "mm"},
         {"debug", 'V', POPT_ARG_NONE, &debug, 0, "Debug"},
         {"quiet", 'q', POPT_ARG_NONE, &quiet, 0, "Quiet"},
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
                   d = 0,
                   e = 0,
                   q = 0;
   tx("G90\n");                 /* absolute */
   waitrx();
   tx("G28.3 X0Y0Z0\n");        /* origin */
   waitrx();
   tx("G1 Z%lfF%d\n", clearance, fast);
   waitrx();
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
   q = (a + b + c + d) / 4;
   //Centre / average
      if (width && height)
      e = z(width / 2, height / 2);
   else
      e = q;
   tx("G1 Z%lfF%d\n", a + clearance, fast);
   waitrx();
   tx("G1 X0Y0F%d\n", fast);    /* home */
   waitrx();
   tx("G28.3 X0Y0Z%d\n", clearance);    /* origin */
   waitrx();
   tx("G1 Z%dF%d\n", park, fast);
   waitrx();
   close(p);
   if (!quiet)
      fprintf(stderr, "Start %lf, Centre mismatch %+.3f, Diagonal mismatch ±%.3f\n", a, e - q, fabs((a + c) / 2 - (b + d) / 2) / 2);
   printf("%lf %lf %+lf %+lf %+lf %+lf\n", width, height, b - a, c - a, d - a, e - a);
   return 0;
}
