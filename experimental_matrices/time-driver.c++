#include <cassert>
#include <iostream>
#include <iterator> // std::iterator_traits
#include <vector>

#include <time.h>
#include "timer.h++"

#include "assert.h"

#ifndef LOAD 
#define LOAD 0.5
#endif

#ifndef LOOKUP
#define LOOKUP 0.8
#endif

#ifndef SIZE
#define SIZE 1000000
#endif

#ifndef PROB_INSERT
#define PROB_INSERT 0.8
#endif

#include "algorithm.i++" // K,V and H

int main() {
  int const size_of_table = SIZE;

  double elems = ((double)size_of_table) * LOAD;
  double lookups = ((double)size_of_table) * LOOKUP;

  int const number_of_elements = (int) elems;
  int const number_of_lookups  = (int) lookups;
  timer clock;

  srand(4242);
  
  H foo(size_of_table);

  std::vector<K> ks(number_of_elements);
  std::vector<V> vs(number_of_elements);
  for (int i = 0; i < number_of_elements; i++) {
      ks[i] = K(unsigned(std::rand()));
      vs[i] = V(unsigned(std::rand()));
  }

  std::vector<K> ls(number_of_lookups);
  for (int i = 0; i < number_of_lookups; i++) {
      ls[i] = K(unsigned(std::rand()));
  }

  clock.start();
  for (int i = 0; i < number_of_elements; ++i) {
    K elem = ks[i];
    V value = vs[i];

    foo.insert(elem, value);
    assert(foo.lookup(elem).value() == value);
  }

  for (int i = 0; i < number_of_lookups; ++i) {
    K elem = ks[i % number_of_elements];
    foo.lookup(elem);
  }
  clock.stop();

  double time = clock.getElapsedTimeMicroSec();
  std::cout << SIZE << " " << LOAD << " " << LOOKUP << " " << time << " microsecs\n";

  return 0;
}
