CXX = c++
CPPFLAGS += -Iinclude
LDLIBS += -lz
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
SANITIZER_FLAGS = -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fno-omit-frame-pointer -fsanitize=address,undefined

CORE_SOURCES := src/text.cpp $(sort $(wildcard src/index/*.cpp src/query/*.cpp src/import/*.cpp))
CLI_SOURCES := src/main.cpp $(sort $(wildcard src/cli/*.cpp))
WEB_SOURCES := $(sort $(wildcard src/web/*.cpp))
TEST_SOURCES := $(sort $(wildcard tests/*.cpp))

CORE_OBJECTS := $(addprefix build/obj/,$(CORE_SOURCES:.cpp=.o))
CLI_OBJECTS := $(addprefix build/obj/,$(CLI_SOURCES:.cpp=.o))
WEB_OBJECTS := $(addprefix build/obj/,$(WEB_SOURCES:.cpp=.o))
TEST_OBJECTS := $(addprefix build/obj/,$(TEST_SOURCES:.cpp=.o))
SCALE_OBJECT := build/obj/tests/tools/scale_check.o
REAL_CHECK_OBJECT := build/obj/tests/tools/check_real_corpus.o
SANITIZER_OBJECTS := $(addprefix build/sanitize/,$(CORE_SOURCES:.cpp=.o) $(TEST_SOURCES:.cpp=.o))
OBJECTS := $(CORE_OBJECTS) $(CLI_OBJECTS) $(WEB_OBJECTS) $(TEST_OBJECTS) $(SANITIZER_OBJECTS) $(SCALE_OBJECT) $(REAL_CHECK_OBJECT)

.PHONY: all web test sanitize scale-test clean

all: build/search

build/search-web: $(CORE_OBJECTS) build/obj/src/cli/output.o $(WEB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -pthread -o $@

build/obj/src/web/http.o: CXXFLAGS += -pthread

web: build/search-web
	./build/search-web

build/search: $(CORE_OBJECTS) $(CLI_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

build/test_search: $(CORE_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

build/scale_check: $(CORE_OBJECTS) $(SCALE_OBJECT)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

build/check_real_corpus: $(CORE_OBJECTS) $(REAL_CHECK_OBJECT)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

scale-test: build/search build/scale_check
	./build/scale_check generate build/scale.jsonl.gz 10000000
	./build/search index build/scale.jsonl.gz build/scale.idx 32
	./build/scale_check check build/scale.idx 10000000

build/test_search_sanitize: $(SANITIZER_OBJECTS)
	$(CXX) $(SANITIZER_FLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

build/obj/%.o: %.cpp
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/sanitize/%.o: %.cpp
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(SANITIZER_FLAGS) -MMD -MP -c $< -o $@

test: build/test_search
	./build/test_search

sanitize: build/test_search_sanitize
	ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/test_search_sanitize

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
