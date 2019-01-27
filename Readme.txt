What you need to run the implementations:
- The directory img_out, where the marked photos will be saved.
- Input directories.
- Watermark images, they must be the same dimensions as the input images.

For the sequential implementation, you must pass as input:
- the path of the input directory, 
- the name of the watermark
- the number of photos.

e.g. ./sequential img_in_sd/ watermark_sd.jpg 11

for the parallel implementations, you must pass as input:
- the path of the input directory,
- the name of the watermark,
- the number of workers
- 1 or any other number to indicate if you want see the statistics for each worker or not
- the waste time, exploited by the emitter as interdeparture time (expressed in microseconds),
- number of photos

e.g. ./farm img_in_sd/ watermark_sd.jpg 3 1 10 11