#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include "CImg.h"
#include <fstream>
#include <atomic>
#include <chrono>
#include "util.cpp"
#include <ctime>
#include <thread>

/*
 This implementation takes as input:
 -  the directory path of the input images,
 -  name of the watermark,
 -  number of threads,
 -  puts 1 if you want print the statistics for the single worker
 -  the waste time (expressed in microseconds), so the delay that the emitter introduces when send the images,
 -  number of photos.
 
 This programs uploads 1 image from the input directory, creats n_photos copies and puts them in the "photos" vector.
 Then the program assignes:
 -   1 thread for the emitter,
 -   as many thread as the number of threads indicated for the wotkers
 -  1 thread for the collector
 
 The Emitter exploit the vector "photo" and sends each element into the queue "original_photos" introducing the indicated delay.
 Each Wotker takes one photo from the "original_photos" queue, compute it and puts the result in the "marked_photos" queue.
 The Collector looks the "marked_photos" queue and sends each element in the "photos_completed" vector.
 
 At the end the program writes the images contained in the "photos_completed" vector in the directory "img_out/".
 
 The program uses some stopwatches to meeasure the time spent in the various parts of the code, all expressed in microseconds.
 In the "msec_emitter" will be saved the time spent for the emitter phase.
 In the "msec_collector" will be saved the time spent for the collecto phase.
 In the "stamp_times" vector will be saved the times of the workers and only the slower will be printed at the end.
 in the "computation_times" vector will be saved the times spent by the workers for only the marking phase and only the slower
 will be printed at the end.
 */

using namespace cimg_library;
struct Photo {
    CImg<unsigned char> *image;
    std::string title;
    Photo(CImg<unsigned char> *image, std::string title) : image(image), title(title){};
};

CImg<unsigned char> *watermark;
int waste_time;
int n_workers;
int print_out;
long msec_emitter = 0;
long msec_collector = 0;
std::vector<Photo *> photos;
std::vector<Photo *> photos_completed;
std::vector<long> stamp_times;
std::vector<long> computation_times;
queue<Photo *> original_photos;
queue<Photo *> marked_photos;

/*
 The emitter takes the elements from the "photos" vector and puts them in the "orginal_photos" queue. At the end
 the emitter puts as many EOS as there are the workers to announce that the stream is finished.
 */
int emitter(){
    auto start_emitter = std::chrono::high_resolution_clock::now();
    for(Photo *ph : photos){
        active_delay(waste_time);
        original_photos.push(ph);
    }
    for(int i = 0; i< n_workers ; i++)
        original_photos.push(EOS);
    
    auto end_emitter = std::chrono::high_resolution_clock::now();
    msec_emitter = std::chrono::duration_cast<std::chrono::microseconds>(end_emitter - start_emitter).count();
    return 0;
}

/*
 The worker exploit some variable to take track of:
 - number of task computed (tn)
 - total time spent (usectot)
 - min and max of usectot
 - total time spent for the marking phase (usec_computation)
 usectot and usec_computation will be saved in the vectors "stamp_times" "and computation_times"
 
 The worker tries to get an element inside the "original_photos" queue and then checks,
 if the element is EOS it forward the EOS in the "marked_photos" and terminates.
 Otherwise computes the marking phase and puts the result in the "marked_photos".
 The marking phase exploits the "watermark" image, for each not white pixel in the watermark, change the pixels of the element
 in the same positions.
 */
int worker(int id){
    int tn = 0;
    long usectot = 0;
    long usecmin = INT_MAX;
    long usecmax = 0;
    long usec_computation = 0;
    while(true){
        auto start_stamp = std::chrono::high_resolution_clock::now();
        Photo *ph = original_photos.pop();
        if(ph != EOS){
            auto start = std::chrono::high_resolution_clock::now();
            cimg_forXY(*(ph->image), x, y){
                if((*watermark)(x,y) != 255){
                    unsigned char valR = (*(ph->image))(x,y,0,0);
                    unsigned char valG = (*(ph->image))(x,y,0,1);
                    unsigned char valB = (*(ph->image))(x,y,0,2);
                    unsigned char avg = ((0.21*valR) + (0.72*valG) + (0.07*valB))/3;
                    (*(ph->image))(x,y,0,0) = (*(ph->image))(x,y,0,1) = (*(ph->image))(x,y,0,2) = avg;
                }
            }
            auto end = std::chrono::high_resolution_clock::now() - start;
            auto usec_light= std::chrono::duration_cast<std::chrono::microseconds>(end).count();
            marked_photos.push(ph);
            auto elapsed = std::chrono::high_resolution_clock::now() - start_stamp;
            auto usec    = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            if(usec < usecmin)
                usecmin = usec;
            if(usec > usecmax)
                usecmax = usec;
            usectot += usec;
            usec_computation += usec_light;
            ++tn;
        }
        else{
            marked_photos.push(EOS);
            stamp_times[id] = usectot;
            computation_times[id] = usec_computation;
            if(print_out == 1  && tn > 0){
                printOut(" Total stamp time " + std::to_string(usectot) + " for thread " + std::to_string(id) + " computed " + std::to_string(tn) + " tasks " + " min max avg = " + std::to_string(usecmin) + " " + std::to_string(usecmax) + " " + std::to_string(usectot/tn));
            }
            return 0;
        }
        
    }
}

/*
 The collector tries to get an element from the "marked_photos" queue and then checks,
 if the element is EOS, the collector increments the counter and only when the counter = number of workers it can terminate.
 Otherwise the collector puts the element inside the "photo_completed" vector.
 */
int collector(){
    auto start_collector = std::chrono::high_resolution_clock::now();
    int counter = 0;
    while(counter < n_workers){
        Photo *ph = marked_photos.pop();
        if (ph == EOS)
            counter ++;
        else
            photos_completed.push_back(ph);
    }
    auto end_collector= std::chrono::high_resolution_clock::now();
    msec_collector = std::chrono::duration_cast<std::chrono::microseconds>(end_collector - start_collector).count();
    return 0;
}

int main(int argc, char * argv[]) {
    
    if (argc != 7){
        std::cout<< "you have to provide a valid directory name where take the images" << std::endl;
        std::cout<< "you have to provide a valide name file for the mark" << std::endl;
        std::cout<< "you have to provide a number of workers" << std::endl;
        std::cout<< "if you want to print the statistics for each worker puts 1" << std::endl;
        std::cout<< "you have to provide a waste time" << std::endl;
        std::cout<< "you have to provide a number of photos" << std::endl;

        return 0;
    }
    
    std::string dir_name = argv[1];
    std::string watermark_file_name = argv[2];
    n_workers = atoi(argv[3]);
    print_out = atoi(argv[4]);
    waste_time = atoi(argv[5]);
    int n_photos = atoi(argv[6]);
    stamp_times.resize(n_workers);
    computation_times.resize(n_workers);
    
    if(dir_name.back() != '/')
        dir_name = dir_name+"/";
    
    watermark = new CImg<unsigned char>((watermark_file_name).c_str());
    std::vector<std::string> list_names;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir (dir_name.c_str())) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
            std::string file_name = ent->d_name;
            if (file_name.find(".jpg")!= std::string::npos){
                list_names.push_back(dir_name + file_name);
            }
        }
        
        closedir (dir);
    } else {
        perror ("");
        return EXIT_FAILURE;
    }
    
    CImg<unsigned char> first_image (list_names[0].c_str());
    for(int i = 0 ; i<n_photos; i++){
        CImg<unsigned char> app_ph = first_image;
        CImg<unsigned char> *app = new CImg<unsigned char>(app_ph);
        Photo *ph = new Photo(app, std::to_string(i)+".jpg");
        photos.push_back(ph);
    }
    
    auto start_tot = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    threads.push_back(std::thread(emitter));
    
    for(int i = 0; i < n_workers; i++){
        threads.push_back(std::thread(worker, i));
    }
    
    threads.push_back(std::thread(collector));
    for (int i=0; i < threads.size(); i++)
        threads[i].join();
    auto end_tot = std::chrono::high_resolution_clock::now();
    auto msec_tot = std::chrono::duration_cast<std::chrono::microseconds>(end_tot - start_tot).count();
    
    long total_computation_time =*max_element(std::begin(computation_times), std::end(computation_times));
    long total_worker_time = *max_element(std::begin(stamp_times), std::end(stamp_times));
    long overhead_worker = total_worker_time - total_computation_time;
    std::wcerr <<  "Total time: " << msec_tot << "\n";
    std::wcerr <<  "Total Emitter: " << msec_emitter << "\n";
    std::wcerr <<  "Total Worker time: " << total_worker_time << "\n";
    std::wcerr <<  "Total Computation time: " << total_computation_time<< "\n";
    std::wcerr <<  "Total Overhead Worker time: " << overhead_worker<< "\n";
    std::wcerr <<  "Total Collector: " << msec_collector << "\n";
    std::wcerr <<  "Number of images: " << photos.size() << "\n";
    std::wcerr <<  "Images dimensions: " << watermark->width() << " : " << watermark->height() << "\n";
    
    for(Photo *ph : photos_completed)
        (*(ph->image)).save_jpeg((std::string("img_out/marked_")+ (ph->title)).c_str());
    
    return 0;
}


