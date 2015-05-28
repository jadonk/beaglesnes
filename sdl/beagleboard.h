#ifndef __BEAGLEBOARD_H__
#define __BEAGLEBOARD_H__

/* Only define one of these! Comment out the other. */
#define BEAGLEBONE_BLACK 1
//#define BEAGLEBOARD_XM 1

#if defined(BEAGLEBONE_BLACK) && defined(BEAGLEBOARD_XM)
#error Only define one board that you are building for in beagleboard.h!
#endif

#if !defined(BEAGLEBONE_BLACK) && !defined(BEAGLEBOARD_XM)
#error You must define one board to build for in beagleboard.h!
#endif
 
#endif /* __BEAGLEBOARD_H__ */

