CXX = g++
CXXFLAGS = -Wall -Wextra -pthread

all: dispatcher ingester processor reporter


dispatcher: dispatcher.cpp common.h
	$(CXX) $(CXXFLAGS) dispatcher.cpp -o dispatcher


ingester: ingester.cpp common.h
	$(CXX) $(CXXFLAGS) ingester.cpp -o ingester


processor: processor.cpp common.h
	$(CXX) $(CXXFLAGS) processor.cpp -o processor


reporter: reporter.cpp common.h
	$(CXX) $(CXXFLAGS) reporter.cpp -o reporter


clean:
	rm -f dispatcher ingester processor reporter
	rm -rf logs sample_output
