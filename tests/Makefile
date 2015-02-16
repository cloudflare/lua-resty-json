SRC := unit_test.cxx test_util.cxx
OBJ := ${SRC:.cxx=.o}

OS := $(shell uname)
ifeq ($(OS), Darwin)
    LIBLJSON := libljson.dylib
else
    LIBLJSON := libljson.so
endif

CXXFLAGS := -Wall -O0 -g -MMD
LDFLAGS := -Wl,-rpath,.. -L.. -lljson

PROGRAM = unit_test

.PHONY = all test

all : $(PROGRAM)
ifeq ($(OS), Darwin)
    # The symbolic link command is a dirty work-around for the fact that
    # json_decoder.lua is looking for the "libljson.so" (hard-coded) in
    # package.cpath.
    #
	test -L ../libljson.so || ln -s libljson.dylib ../libljson.so
	DYLD_LIBRARY_PATH=$$(DYLD_LIBRARY_PATH):.. ./$(PROGRAM) ./$(PROGRAM)
else
	./$(PROGRAM)
endif
	@echo
	@echo "Testing Lua wrapper..."
	luajit ./test.lua

$(PROGRAM) : $(OBJ) ../$(LIBLJSON)
	$(CXX) $(OBJ) $(LDFLAGS) -o $@
	@-cat *.d > dep.txt

${OBJ} : %.o : %.cxx
	$(CXX) $(CXXFLAGS) -c $<

-include dep.txt

clean:
	rm -f *.txt *.d *.o $(PROGRAM)
