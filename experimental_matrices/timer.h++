/*
  This high-resolution timer is able to measure the elapsed time with
  one microsecond accuracy

  Author: Song Ho Ahn (song.ahn@gmail.com) © 2003, 2006
*/

#include <sys/time.h>

class timer {
public:

  timer();
  ~timer();

  void start();
  void stop();
  double getElapsedTime();        // get elapsed time in seconds
  double getElapsedTimeSec();     // same as getElapsedTime
  double getElapsedTimeMilliSec();// get elapsed time in milliseconds
  double getElapsedTimeMicroSec();// get elapsed time in microseconds
  double getElapsedTimeNanoSec(); // get elapsed time in nanoseconds

private:

  double startTimeMicroSec;       // starting time in microseconds
  double endTimeMicroSec;         // ending time in microseconds
  int stopped;                    // stop flag 
  timeval startCount;     
  timeval endCount;
};

#include "timer.i++"