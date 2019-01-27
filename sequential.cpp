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

using namespace cimg_library;

/*
 This implementation takes as input:
 -  the directory path of the input images,
 -  name of the watermark,
 -  number of photos. 
 
 This programs uploads 1 image from the input directory and then creats n_photos copies.
 Then iterates over the vector and perform the marking phase.
 The marking phase exploits the "watermark" image, for each not white pixel in the watermark, change the pixels of the element
 in the same positions.
 At the end the program writes the images in the directory "img_out/".
 */

struct Photo {
    CImg<unsigned char> *image;
    std::string title;
    Photo(CImg<unsigned char> *image, std::string title) : image(image), title(title){};
};

int main(int argc, char * argv[]) {
    
    if (argc != 4){
        std::cout<< "you have to provide a valid directory name where take the images" << std::endl;
        std::cout<< "you have to provide a valide name file for the mark" << std::endl;
        std::cout<< "you have to provide a number of photos" << std::endl;
        return 0;
    }
    
    std::string dir_name = argv[1];
    std::string watermark_file_name = argv[2];
    int n_photos = atoi(argv[3]);
    
    if(dir_name.back() != '/')
        dir_name = dir_name+"/";
    
    CImg<unsigned char> *watermark = new CImg<unsigned char>((watermark_file_name).c_str());
    std::vector<Photo *> photos;
    std::vector<std::string> list_names;

    auto start_tot = std::chrono::high_resolution_clock::now();
    auto start_load = std::chrono::high_resolution_clock::now();


    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir (dir_name.c_str())) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
            std::string file_name = ent->d_name;
            if (file_name.find(".jpg")!= std::string::npos){
                std::string file_name =  ent->d_name;
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
        CImg<unsigned char> *app = new CImg<unsigned char>(first_image);
        Photo *ph = new Photo(app, std::to_string(i)+".jpg");
        photos.push_back(ph);
    }
    auto end_load = std::chrono::high_resolution_clock::now();
    auto msec_load = std::chrono::duration_cast<std::chrono::microseconds>(end_load - start_load).count();
    
    auto start_stamp = std::chrono::high_resolution_clock::now();
    for(Photo *ph : photos){
        cimg_forXY(*(ph->image), x, y){
            if((*watermark)(x,y) != 255){
                unsigned char valR = (*(ph->image))(x,y,0,0);
                unsigned char valG = (*(ph->image))(x,y,0,1);
                unsigned char valB = (*(ph->image))(x,y,0,2);
                unsigned char avg = ((0.21*valR) + (0.72*valG) + (0.07*valB))/3;
                (*(ph->image))(x,y,0,0) = (*(ph->image))(x,y,0,1) = (*(ph->image))(x,y,0,2) = avg;
            }
        }
    }
    auto end_stamp = std::chrono::high_resolution_clock::now();
    auto msec_stamp = std::chrono::duration_cast<std::chrono::microseconds>(end_stamp - start_stamp).count();
    
    
    auto start_write = std::chrono::high_resolution_clock::now();
    for(int i = 0; i<photos.size(); i++)
        (*(photos[i]->image)).save_jpeg((std::string("img_out/marked_")+  (photos[i]->title)).c_str());
    auto end_write = std::chrono::high_resolution_clock::now();
    auto msec_write = std::chrono::duration_cast<std::chrono::microseconds>(end_write - start_write).count();
    
    auto end_tot = std::chrono::high_resolution_clock::now();
    auto msec_tot = std::chrono::duration_cast<std::chrono::microseconds>(end_tot - start_tot).count();
    
    std::wcerr <<  "Total time: " << msec_tot << "\n";
    std::wcerr <<  "Total load time: " << msec_load << "\n";
    std::wcerr <<  "Total stamp time: " << msec_stamp << "\n";
    std::wcerr <<  "Total write time: " << msec_write << "\n";
    std::wcerr <<  "Number of images: " << photos.size() << "\n";
    std::wcerr <<  "Images dimensions: " << watermark->width() << " x " << watermark->height() << "\n";

    return 0;
}


