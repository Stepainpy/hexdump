#include <fstream>

int main() {
    char buf[256];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = i;
    std::ofstream file("test-hd.dat", std::ios::binary);
    file.write(buf, sizeof(buf));
    file.close();
}