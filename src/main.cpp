#include <iostream>
#include <fstream>

using std::cout;
using std::endl;
using std::string;
extern int play(const char* file);


int main(int argc, char* argv[]) {
    cout << "hello, simplePlayer." << endl;
    if (argc != 2) {
        cout << "input error:" << endl;
        cout << "arg[1] should be the media file." << endl;
    }
    else {
        auto inputPath = argv[1];
        cout << "play file:" << inputPath << endl;
        play(inputPath);
    }
    return 0;
}