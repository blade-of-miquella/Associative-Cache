#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

const int RAM_SIZE = 512;          
const int BLOCK_SIZE = 8;          
const int NUM_BLOCKS_RAM = RAM_SIZE / BLOCK_SIZE;  

const int CACHE_WAYS = 4;
const int CACHE_NUM_SETS = NUM_BLOCKS_RAM / (CACHE_WAYS * CACHE_WAYS); 

struct CacheLine {
    bool valid;                 
    int tag;                    
    vector<int> data;           
    unsigned long fifo_time;
};

vector<int> RAM(RAM_SIZE, 0);
vector<vector<CacheLine>> cache(CACHE_NUM_SETS, vector<CacheLine>(CACHE_WAYS));

unsigned long globalTime = 0;
unsigned long hitCount = 0;
unsigned long missCount = 0;

void initCache() {
    for (int set = 0; set < CACHE_NUM_SETS; set++) {
        for (int way = 0; way < CACHE_WAYS; way++) {
            cache[set][way].valid = false;
            cache[set][way].tag = -1;
            cache[set][way].data.assign(BLOCK_SIZE, 0);
            cache[set][way].fifo_time = 0;
        }
    }
}

void initRAM() {
    for (int i = 0; i < RAM_SIZE; i++) {
        RAM[i] = i;
    }
}

int readFromAddress(int address, bool debug = false) {
    if (address < 0 || address >= RAM_SIZE) {
        cout << "Invalid address!" << endl;
        return -1;
    }

    int blockNumber = address / 128;
    int offset = address % BLOCK_SIZE;
    int lineIndex = (address % 128) / BLOCK_SIZE; 
    int tag = blockNumber * 4 + (address / 32) % 4;
    int setIndex = blockNumber % CACHE_NUM_SETS;

    if (debug) {
        cout << "\nReading from address " << address << ":\n"
             << "  Block number: " << blockNumber
             << ", offset: " << offset << ", set index: " << setIndex
             << ", tag: " << tag << endl;
    }

    for (int way = 0; way < CACHE_WAYS; way++) {
        if (cache[setIndex][way].valid && cache[setIndex][way].tag == tag) {
            hitCount++;
            if (debug)
                cout << "  CACHE HIT (set " << setIndex << ", way " << way << ")\n";
            return cache[setIndex][way].data[offset];
        }
    }

    missCount++;
    if (debug)
        cout << "  CACHE MISS (set " << setIndex << "). Loading block from RAM.\n";

    int replaceIndex = 0;
    bool foundEmpty = false;
    for (int way = 0; way < CACHE_WAYS; way++) {
        if (!cache[setIndex][way].valid) {
            replaceIndex = way;
            foundEmpty = true;
            break;
        }
    }

    if (!foundEmpty) {
        unsigned long minTime = cache[setIndex][0].fifo_time;
        replaceIndex = 0;
        for (int way = 1; way < CACHE_WAYS; way++) {
            if (cache[setIndex][way].fifo_time < minTime) {
                minTime = cache[setIndex][way].fifo_time;
                replaceIndex = way;
            }
        }
    }

    cache[setIndex][replaceIndex].valid = true;
    cache[setIndex][replaceIndex].tag = tag;
    cache[setIndex][replaceIndex].fifo_time = globalTime++;

    for (int i = 0; i < BLOCK_SIZE; i++) {
        int ramAddress = blockNumber * 128 + lineIndex * BLOCK_SIZE + i;
        cache[setIndex][replaceIndex].data[i] = (ramAddress < RAM_SIZE ? RAM[ramAddress] : 0);
    }

    if (debug) {
        cout << "  Loaded block stored in set " << setIndex
             << ", way " << replaceIndex << endl;
    }

    return cache[setIndex][replaceIndex].data[offset];
}



void writeToAddress(int address, int value, bool debug = false) {
    if (address < 0 || address >= RAM_SIZE) {
        cout << "Invalid address!" << endl;
        return;
    }
    int blockNumber = address / BLOCK_SIZE;
    int offset = address % BLOCK_SIZE;
    int setIndex = blockNumber % CACHE_NUM_SETS;
    int tag = blockNumber / CACHE_NUM_SETS;
    RAM[address] = value;
    bool found = false;
    for (int way = 0; way < CACHE_WAYS; way++) {
        if (cache[setIndex][way].valid && cache[setIndex][way].tag == tag) {
            cache[setIndex][way].data[offset] = value;
            found = true;
            if (debug)
                cout << "  Write to cache (HIT) in set " << setIndex
                << ", way " << way << endl;
            break;
        }
    }
    if (!found) {
        if (debug)
            cout << "  CACHE MISS on write. Loading block in cache (write-allocate).\n";
        readFromAddress(address, debug);
        for (int way = 0; way < CACHE_WAYS; way++) {
            if (cache[setIndex][way].valid && cache[setIndex][way].tag == tag) {
                cache[setIndex][way].data[offset] = value;
                break;
            }
        }
    }
}

void displayRAM() {
    cout << "\nRAM contents:" << endl;
    for (int i = 0; i < RAM_SIZE; i++) {
        cout << "[" << i << "]=" << RAM[i] << " ";
        if ((i + 1) % BLOCK_SIZE == 0)
            cout << endl;
    }
}

void displayCache() {
    cout << "\nCache contents:" << endl;
    for (int set = 0; set < CACHE_NUM_SETS; set++) {
        cout << "Set " << set << ":" << endl;
        for (int way = 0; way < CACHE_WAYS; way++) {
            cout << "  Way " << way << " | ";
            if (cache[set][way].valid)
                cout << "Tag: " << cache[set][way].tag << " , Data: ";
            else
                cout << "Empty line, Data: ";
            for (int i = 0; i < BLOCK_SIZE; i++) {
                cout << cache[set][way].data[i] << " ";
            }
            cout << endl;
        }
    }
}

void showStatistics() {
    unsigned long totalAccesses = hitCount + missCount;
    cout << "\nCache statistics:" << endl;
    cout << "  Total accesses: " << totalAccesses << endl;
    cout << "  Hits: " << hitCount << " ("
        << (totalAccesses ? (100.0 * hitCount / totalAccesses) : 0) << "%)" << endl;
    cout << "  Misses: " << missCount << " ("
        << (totalAccesses ? (100.0 * missCount / totalAccesses) : 0) << "%)" << endl;

    int usedLines = 0;
    long long sum = 0;
    int count = 0;
    for (int set = 0; set < CACHE_NUM_SETS; set++) {
        for (int way = 0; way < CACHE_WAYS; way++) {
            if (cache[set][way].valid) {
                usedLines++;
                for (int i = 0; i < BLOCK_SIZE; i++) {
                    sum += cache[set][way].data[i];
                    count++;
                }
            }
        }
    }
    cout << "  Filled cache lines: " << usedLines
        << " out of " << (CACHE_NUM_SETS * CACHE_WAYS) << endl;
    if (count > 0)
        cout << "  Average value in cache: " << (sum / (double)count) << endl;
}

void simulateSequentialAccess() {
    int requests = 0, beginAddr = 0;
    cout << "Enter number of requests: ";
    cin >> requests;
    cout << "Enter start address(0-511): ";
    cin >> beginAddr;
    cout << "\nSimulating sequential access..." << endl;
    for (int addr = beginAddr; addr < requests + beginAddr; addr++) {
        if(addr <= RAM_SIZE) readFromAddress(addr);
    }
    cout << "Sequential access simulation complete" << endl;
}

void simulateRandomAccess() {
    int n;
    cout << "\nEnter the number of random accesses: ";
    cin >> n;
    cout << "Simulating random access..." << endl;
    for (int i = 0; i < n; i++) {
        int addr = rand() % RAM_SIZE;
        readFromAddress(addr);
    }
    cout << "Random access simulation complete" << endl;
}

void simulateLocalAccess() {
    int requests = 0, localityRange = 0, numRegions = 0;
    cout << "Enter number of requests per region: ";
    cin >> requests;
    cout << "Enter locality range: ";
    cin >> localityRange;
    cout << "Enter number of local regions: ";
    cin >> numRegions;
    cout << "\nSimulating multiple local access regions..." << endl;
    for (int region = 0; region < numRegions; region++) {
        int start = rand() % (RAM_SIZE - localityRange);
        cout << "Accessing region " << region + 1 << " starting at address " << start << endl;
        for (int i = 0; i < requests; i++) {
            int addr = start + (rand() % localityRange); 
            if (addr < RAM_SIZE) { 
                readFromAddress(addr);
            }
        }
    }
    cout << "Local access simulation complete" << endl;
}

int main() {
    srand(time(NULL));
    initRAM();
    initCache();

    int choice;
    do {
        cout << "\nMenu:" << endl;
        cout << "1. Load RAM data from file" << endl;
        cout << "2. Display RAM contents" << endl;
        cout << "3. Display cache contents" << endl;
        cout << "4. Read from address (manual)" << endl;
        cout << "5. Write to address (manual)" << endl;
        cout << "6. Simulate sequential access" << endl;
        cout << "7. Simulate random access" << endl;
        cout << "8. Simulate local access" << endl;
        cout << "9. Show cache statistics" << endl;
        cout << "0. Exit" << endl;
        cout << "Enter your choice: ";
        cin >> choice;

        switch (choice) {
        case 1: {
            string filename;
            cout << "Enter filename: ";
            cin >> filename;
            ifstream fin(filename);
            if (!fin) {
                cout << "Error opening file!" << endl;
            }
            else {
                int index = 0;
                while (fin >> RAM[index] && index < RAM_SIZE) {
                    index++;
                }
                cout << index << " numbers loaded into RAM." << endl;
            }
            break;
        }
        case 2:
            displayRAM();
            break;
        case 3:
            displayCache();
            break;
        case 4: {
            int addr;
            cout << "\nEnter address to read (0 - " << RAM_SIZE - 1 << "): ";
            cin >> addr;
            int val = readFromAddress(addr, true);
            cout << "Value at address " << addr << ": " << val << endl;
            break;
        }
        case 5: {
            int addr, val;
            cout << "\nEnter address to write (0 - " << RAM_SIZE - 1 << "): ";
            cin >> addr;
            cout << "Enter value: ";
            cin >> val;
            writeToAddress(addr, val, true);
            cout << "Value " << val << " written at address " << addr << endl;
            break;
        }
        case 6:
            simulateSequentialAccess();
            break;
        case 7:
            simulateRandomAccess();
            break;
        case 8:
            simulateLocalAccess();
            break;
        case 9:
            showStatistics();
            break;
        case 0:
            cout << "\nExiting..." << endl;
            break;
        default:
            cout << "Invalid option!" << endl;
        }
    } while (choice != 0);

    return 0;
}
