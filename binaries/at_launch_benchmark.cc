#include "ATen/Parallel.h"

#include "c10/util/Flags.h"
#include "caffe2/core/init.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <ctime>

C10_DEFINE_int(iter, 10e4, "Number of at::launch iterations (tasks)");
C10_DEFINE_int(warmup_iter, 10, "Number of warmup iterations")
C10_DEFINE_int(inter_op_threads, 0, "Number of inter-op threads");
C10_DEFINE_bool(init_caffe2, false, "Initialize Caffe2");

namespace {
int iter = 0;
std::atomic<int> counter{0};
std::condition_variable cv;
std::mutex mutex;
}

 void launch_tasks() {
  at::launch([]() {
    at::launch([](){
      at::launch([]() {
        auto cur_ctr = ++counter;
        if (cur_ctr == iter) {
          std::unique_lock<std::mutex> lk(mutex);
          cv.notify_one();
        }
      });
    });
  });
}

void launch_tasks_and_wait(int tasks_num) {
  iter = tasks_num;
  counter = 0;
  for (auto idx = 0; idx < iter; ++idx) {
    launch_tasks();
  }
  {
    std::unique_lock<std::mutex> lk(mutex);
    while (counter < iter) {
      cv.wait(lk);
    }
  }
}

int main(int argc, char** argv) {
  if (FLAGS_init_caffe2) {
    caffe2::GlobalInit(&argc, &argv);
  } else if (!c10::ParseCommandLineFlags(&argc, &argv)) {
    std::cout << "Failed to parse command line flags" << std::endl;
    return -1;
  }

  at::init_num_threads();

  if (FLAGS_inter_op_threads > 0) {
    at::set_num_interop_threads(FLAGS_inter_op_threads);
  }

  launch_tasks_and_wait(FLAGS_warmup_iter);

  std::cout << "Launching " << FLAGS_iter << " tasks using "
            << at::get_num_interop_threads() << " threads "
            << std::endl;

  typedef std::chrono::high_resolution_clock clock;
  typedef std::chrono::milliseconds ms;
  std::chrono::time_point<clock> start_time = clock::now();

  launch_tasks_and_wait(FLAGS_iter);

  auto duration = static_cast<float>(
      std::chrono::duration_cast<ms>(clock::now() - start_time).count());
  std::cout << "Time to run " << iter << " iterations "
            << (duration/1000.0) << " s." << std::endl;

  return 0;
}
