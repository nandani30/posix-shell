CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -pedantic -g

TARGET = myshell
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

test: all
	@chmod +x test.sh
	@./test.sh $(OBJS)

.PHONY: all clean
