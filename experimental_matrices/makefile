CXX=g++
CXXFLAGS=-O3 -std=c++17 -x c++ -Wall -Wextra -DNDEBUG
#CXXFLAGS=-Ofast -std=c++17 -x c++ -Wall -Wextra -DNDEBUG
IFLAGS = -I..

size = 100 200 300 400 500 600 700 800 900 1000 1100 1200 1300 1400 1500 1600 1700 1800 1900 2000 2100 2200 2300 2400
load = 0.1 0.2 0.3

time:
	@for x in $(size) ; do \
		for load_factor in $(load) ; do \
			echo $$x $$load_factor | python generate_sparse_matrix.py; \
			$(CXX) $(CXXFLAGS) -DSIZE=$$x -DLOAD=\"$$load_factor\" dense.cpp; \
			./a.out; \
			rm -f ./a.out ; \
		done \
	done

clean:
	@rm -vf *~ a.out temp plot.* && rm algorithm.i++ || true

