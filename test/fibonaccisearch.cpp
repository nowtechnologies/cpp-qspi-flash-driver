#include<algorithm>
#include<iostream>
#include<numeric>
#include<iomanip>
#include<vector>
#include<string>
#include<limits>
#include<cmath>

int main(int argc, char **argv) {
  int ret;
  if(argc > 1) {
    std::string arg(argv[1]);
    int length = std::stoi(arg);
    int displacement = -1;
    int maxSoFar = std::numeric_limits<int>::max();
    for(int d = length / 4; d <= 3 * length / 4; ++d) {
      if(std::gcd(d ,length) == 1) {
        int m = length % d;
        int n = std::min(m, d - m);
        int value = std::abs(d * (d - 3 * n) + n * n);
        if(value < maxSoFar) {
          maxSoFar = value;
          displacement = d;
        }
        else { // nothing to do
        }
      }
      else { // nothing to do
      }
    }
    if(displacement > 0) {
      std::cout << "displacement: " << displacement << " ratio: " << static_cast<double>(displacement) / length << " maxSoFar: " << maxSoFar << '\n';
      int where = 0;
      std::vector<bool> was(length, false);
      for(int i = 0; i < length; ++i) {
        was[where] = true;
        where = (where + displacement) % length;
        int maxFree = 0;
        std::vector<bool>::iterator found0;
        std::vector<bool>::iterator found1 = was.begin();
        do {
          found0 = std::find(found1, was.end(), false);
          found1 = std::find(found0, was.end(), true);
          maxFree = std::max(static_cast<int>(found1 - found0), maxFree);
        } while(found0 != was.end());
        std::cout << std::setw(4) << i << ' ' << std::setw(4) << (length + i) / (i + 1) << std::setw(4) << maxFree << " - ";
        for(int j = 0; j < length; ++j) {
          std::cout << (was[j] ? static_cast<char>(('0' + j % 10)) : ' ');
        }
        std::cout << '\n';
      }
      ret = 0;
    }
    else {
      std::cerr << "No suitable displacement found.\n";
      ret = 1;
    }
  }
  else {
    std::cerr << "Usage: " << argv[0] << " [length of partition]\n";
    ret = 1;
  }
  return ret;
}
