#include <iostream>
#include <vector>
#include <ff/pipeline.hpp>
#include <ff/farm.hpp>
#include <string>
#include "CImg.h"
#include <fstream>
#include <atomic>
#include <ctime>
#include <algorithm>
#include "util.cpp"
#include <map>

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
 
 The Emitter exploit the vector "photo" and sends a portion of each element to all the workers introducing the indicated delay.
 Each Wotker takes one photo and the related chunk from the input channel, compute it and sends the results.
 The Collector looks the input elements and sends the final element in the "photos_completed" vector.
 
 At the end the program writes the images contained in the "photos_completed" vector in the directory "img_out/".
 
 The program uses some stopwatches to meeasure the time spent in the various parts of the code, all expressed in microseconds.
 In the "msec_emitter" will be saved the time spent for the emitter phase.
 In the "msec_collector" will be saved the time spent for the collecto phase.
 In the "stamp_times" vector will be saved the times of the workers and only the slower will be printed at the end.
 */
using namespace ff;
using namespace cimg_library;

struct Photo {
    CImg<unsigned char> *image;
    std::string title;
    int chunk;
    int start_chunk;
    Photo(CImg<unsigned char> *image, int n, int on, std::string title) : image(image), chunk(n), start_chunk(on), title(title){};
};

CImg<unsigned char> *watermark;
int waste_time;
int n_workers;
int print_out = 0;
long msec_emitter = 0;
long msec_collector = 0;
std::vector<Photo *> photos;
std::vector<Photo *> photos_completed;
std::vector<long> stamp_times;
std::map<std::string, std::vector<Photo *>> results;


/*
 The worker exploit some variable to take track of:
 - number of task computed (tn)
 - total time spent (usectot)
 - min and max of usectot
 - total time spent for the marking phase (usec_computation)
 usectot and usec_computation will be saved in the vectors "stamp_times" "and computation_times"
 
 The worker get an element from the input channel and computes the marking phase and puts the result in output channel.
 The marking phase exploits the "watermark" image, for each not white pixel in the watermark' s chunk, change the pixel of the element
 in the same position.
 */
struct Worker: ff_node_t<Photo> {
    int tn = 0;
    long usectot = 0;
    long usecmin = INT_MAX;
    long usecmax = 0;
    int svc_init() {
        return 0;
    }
    Photo *svc(Photo *ph){
        auto start_stamp = std::chrono::high_resolution_clock::now();
        if(ph != EOS){
            for(int y = 0; y<ph->chunk; y++){
                cimg_forX(*(ph->image), i){
                    int j = y + ph->start_chunk;
                    if((*watermark)(i,j) != 255){
                        unsigned char valR = (*(ph->image))(i,j,0,0);
                        unsigned char valG = (*(ph->image))(i,j,0,1);
                        unsigned char valB = (*(ph->image))(i,j,0,2);
                        unsigned char avg = ((0.21*valR) + (0.72*valG) + (0.07*valB))/3;
                        (*(ph->image))(i,j,0,0) = (*(ph->image))(i,j,0,1) = (*(ph->image))(i,j,0,2) = avg;
                    }
                }
                
            }
            auto elapsed = std::chrono::high_resolution_clock::now() - start_stamp;
            auto usec    = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            if(usec < usecmin)
                usecmin = usec;
            if(usec > usecmax)
                usecmax = usec;
            usectot += usec;
            ++tn;
            
            return ph;
        }
    }
    void svc_end(){
        stamp_times[get_my_id()]=usectot;
        if(print_out == 1  && tn > 0){
            printOut(" Total stamp time " + std::to_string(usectot) + " for thread " + std::to_string(get_my_id()) + " computed " + std::to_string(tn) + " tasks " + " min max avg = " + std::to_string(usecmin) + " " + std::to_string(usecmax) + " " + std::to_string(usectot/tn));
        }
    }
    
};
/*
 The emitter takes the elements from the "photos" vector,
 divides the height of the element for the number of the workers + 1 more row. The emitter starts to assignes the chunk and the chunk_assigned to the workers and when the rest of the division is equal to the number of workers already assigned, the emitter decremets the chunk by 1 and continue to assignes the new chunk for the workers remaining.
 */
 struct firstStage: ff_node_t<int>{
    int *svc(int *) {
        Photo *app;
        auto start = std::chrono::high_resolution_clock::now();
        for(Photo *ph : photos){
            int chunk =(ph->image->height()/n_workers)+1;
            int rest_chunk =(ph->image->height()%n_workers);
            int chunk_assigned = 0;
            for(int i = 0; i<n_workers; i++){
                if(i == rest_chunk)
                    chunk --;
                app = new Photo(ph->image, chunk, chunk_assigned, ph->title);
                chunk_assigned += chunk;
                active_delay(waste_time);
                ff_send_out(app);
                
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        msec_emitter = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
        return EOS;
    }
} Emitter;

/*
 The collector gets the elements from the input channel and puts each element inside the "results" map and only when the size of the vector of the map is equal to the number of workers, the collector puts the element inside the "photos_completed" vector.
 */
Photo * writeImage(Photo *ph, ff_node *const){
    auto start = std::chrono::high_resolution_clock::now();
    results[ph->title].push_back(ph);
    if(results[ph->title].size() == n_workers){
        photos_completed.push_back(ph);
        auto end = std::chrono::high_resolution_clock::now();
        msec_collector += std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
        return (Photo *)GO_ON;
        }
    return (Photo *)GO_ON;
}

struct lastStage: ff_node_t<Photo>{
    Photo *svc(Photo *ph) {
        return  writeImage(ph, this);
    }
    
}Collector;


int main(int argc, char *argv[]) {
    if (argc != 7){
        std::cout<< "you have to provide a valid directory name where take the images" << std::endl;
        std::cout<< "you have to provide a valide name file for the mark" << std::endl;
        std::cout<< "you have to provide a number of workers" << std::endl;
        std::cout<< "if you want to print the statistics for each worker puts 1" << std::endl;
        std::cout<< "you have to provide a waste time" << std::endl;
        std::cout<< "you have to provide the number of photos" << std::endl;

        return 0;
    }
    
    std::string dir_name = argv[1];
    std::string watermark_file_name = argv[2];
    n_workers = atoi(argv[3]);
    print_out = atoi(argv[4]);
    waste_time = atoi(argv[5]);
    int n_photos = atoi(argv[6]);
    stamp_times.resize(n_workers);
    
    if(dir_name.back() != '/')
        dir_name = dir_name+"/";
    stamp_times.resize(n_workers);
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
        Photo *ph = new Photo(app, 0,0,std::to_string(i)+".jpg");
        photos.push_back(ph);
    }
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::unique_ptr<ff_node>> W;
    for(int i = 0; i<n_workers; ++i)
        W.push_back(make_unique<Worker>());
    
    ff_Farm<long> farm(std::move(W), Emitter, Collector);
    farm.set_scheduling_ondemand();
    if (farm.run_and_wait_end()<0)
        error("running farm");
    
    auto end = std::chrono::high_resolution_clock::now();
    auto diff = end - start;
    auto msec_tot = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();

    std::wcerr <<  "Total time: " << msec_tot << "\n";
    std::wcerr <<  "Total Emitter: " << msec_emitter << "\n";
    std::wcerr <<  "Total Worker time: " << *max_element(std::begin(stamp_times), std::end(stamp_times)) << "\n";
    std::wcerr <<  "Total Collector: " << msec_collector << "\n";
    std::wcerr <<  "Number of images: " << photos.size() << "\n";
    std::wcerr <<  "Images dimensions: " << watermark->width() << " : " << watermark->height() << "\n";

    for(Photo *ph : photos_completed)
        (*(ph->image)).save_jpeg((std::string("img_out/marked_")+ (ph->title)).c_str());
    
    return 0;
}

