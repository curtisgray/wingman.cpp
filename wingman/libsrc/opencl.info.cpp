// copyright 2013 Jens Schwarzer (schwarzer@schwarzer.dk)
// OpenCL info dump

//#define CL_HPP_TARGET_OPENCL_VERSION 120  // define OpenCL version 1.2
//#define CL_USE_DEPRECATED_OPENCL_1_1_APIS // define CL_USE_DEPRECATED_OPENCL_1_1_APIS until further
#define CL_HPP_ENABLE_EXCEPTIONS            // enable exceptions

#if defined(__APPLE__) || defined(__MACOSX)
#  include <OpenCL/opencl.hpp>
#else
#  include <CL/opencl.hpp>
#endif
#include <map>
#include <string>
#include <vector>

#include <iostream>
#include <numeric>

using std::cout;

// macros to reduce pollution
//#define P(obj, w) cout << #w << ": " << obj.getInfo<w>() << "\n";
//#define Pbool(obj, w) cout << std::boolalpha << #w << ": " << static_cast<bool>(obj.getInfo<w>()) << "\n";
//
//#define PbitmapStart(obj, w) { unsigned bitmap = obj.getInfo<w>(); cout << #w << ": ";
//#define PbitmapTest(w) if (bitmap & w) cout << #w " ";
//#define PbitmapEnd cout << "\n"; }
//
//#define PconstStart(obj, w) { unsigned constant = obj.getInfo<w>(); cout << #w << ": ";
//#define PconstTest(w) if (constant == w) cout << #w "\n";
//#define PconstEnd }


// macros to reduce pollution
//#define P(obj, w) ret[platform][#w] = (obj).getInfo<w>();
//#define Pbool(obj, w) ret[platform][#w] = static_cast<bool>((obj).getInfo<w>()) ? "true" : "false";
//
//#define PbitmapStart(obj, w) { unsigned bitmap = (obj).getInfo<w>();
//#define PbitmapTest(w) ret[platform][#w] = (bitmap & (w)) ? "true" : "false";
//#define PbitmapEnd }
//
//#define PconstStart(obj, w) { unsigned constant = (obj).getInfo<w>();
//#define PconstTest(w) if (constant == (w)) ret[platform][#w].append(#w);
//#define PconstEnd }

#define P(obj, w) ret[platformName][#w] = (obj).getInfo<w>()
#define Pbool(obj, w) ret[platformName][#w] = static_cast<bool>((obj).getInfo<w>()) ? "true" : "false"

#define PbitmapStart(obj, w) { unsigned bitmap = (obj).getInfo<w>(); std::string key = #w; std::vector<std::string> list;
#define PbitmapTest(w) if (bitmap & (w)) list.push_back(#w)
#define PbitmapEnd ret[platformName][key] = join(list);}

#define PconstStart(obj, w) { unsigned constant = (obj).getInfo<w>(); std::string key = #w; std::vector<std::string> list;
#define PconstTest(w) if (constant == (w)) list.push_back(#w);
#define PconstEnd ret[platformName][key] = join(list);}

std::string join(std::vector<std::string> list, const std::string &delimiter = ", ")
{
	if (list.empty())
		return "";
	return std::accumulate(std::next(list.begin()), list.end(), list[0],
		[&](const std::string &a, const std::string &b) {
		return a + delimiter + b;
	}
	);
}

//std::map<cl::Platform, std::map<std::string, std::string>> getCLPlatformDevices()
std::map< std::string, std::map<std::string, std::string> > getCLPlatformDevices()
{
	std::map < std::string, std::map<std::string, std::string> > ret;
	try {
		std::vector<cl::Platform> platforms;
		(void)cl::Platform::get(&platforms);
		//cout << "Number of platforms: " << platforms.size() << "\n";

		// dump platform information
		for (const auto &platform : platforms) {
			//P(platform, CL_PLATFORM_PROFILE);
			//P(platform, CL_PLATFORM_VERSION);
			//P(platform, CL_PLATFORM_NAME);
			//P(platform, CL_PLATFORM_VENDOR);
			//P(platform, CL_PLATFORM_EXTENSIONS);
			const auto platformName = platform.getInfo<CL_PLATFORM_NAME>();

			std::vector<cl::Device> devices;
			(void)platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
			//cout << "Number of devices: " << devices.size() << "\n";

			// dump device information
			for (const auto &device : devices) {
				PbitmapStart(device, CL_DEVICE_TYPE);
				PbitmapTest(CL_DEVICE_TYPE_CPU);
				PbitmapTest(CL_DEVICE_TYPE_GPU);
				PbitmapTest(CL_DEVICE_TYPE_ACCELERATOR);
				PbitmapTest(CL_DEVICE_TYPE_DEFAULT);
				PbitmapTest(CL_DEVICE_TYPE_CUSTOM);
				PbitmapEnd;

				P(device, CL_DEVICE_VENDOR_ID);
				P(device, CL_DEVICE_MAX_COMPUTE_UNITS);
				P(device, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS);

				{
					std::vector<size_t> sizes = device.getInfo<CL_DEVICE_MAX_WORK_ITEM_SIZES>();
					const auto key = "CL_DEVICE_MAX_WORK_ITEM_SIZES";
					auto list = std::vector<std::string>();
					for (auto size : sizes) {
						//cout << size << " ";
						list.push_back(std::to_string(size));
					}
					//cout << "\n";
					ret[platformName][key] = join(list);
				}

				P(device, CL_DEVICE_MAX_WORK_GROUP_SIZE);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE);
				P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_INT);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE);
				P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF);
				P(device, CL_DEVICE_MAX_CLOCK_FREQUENCY);
				P(device, CL_DEVICE_ADDRESS_BITS);
				P(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE);

				Pbool(device, CL_DEVICE_IMAGE_SUPPORT);

				P(device, CL_DEVICE_MAX_READ_IMAGE_ARGS);
				P(device, CL_DEVICE_MAX_WRITE_IMAGE_ARGS);
				P(device, CL_DEVICE_IMAGE2D_MAX_WIDTH);
				P(device, CL_DEVICE_IMAGE2D_MAX_HEIGHT);
				P(device, CL_DEVICE_IMAGE3D_MAX_WIDTH);
				P(device, CL_DEVICE_IMAGE3D_MAX_HEIGHT);
				P(device, CL_DEVICE_IMAGE3D_MAX_DEPTH);
				// P(device, CL_DEVICE_IMAGE_MAX_BUFFER_SIZE);
				// P(device, CL_DEVICE_IMAGE_MAX_ARRAY_SIZE);
				P(device, CL_DEVICE_MAX_SAMPLERS);
				P(device, CL_DEVICE_MAX_PARAMETER_SIZE);
				P(device, CL_DEVICE_MEM_BASE_ADDR_ALIGN);

				PbitmapStart(device, CL_DEVICE_SINGLE_FP_CONFIG);
				PbitmapTest(CL_FP_DENORM);
				PbitmapTest(CL_FP_INF_NAN);
				PbitmapTest(CL_FP_ROUND_TO_NEAREST);
				PbitmapTest(CL_FP_ROUND_TO_ZERO);
				PbitmapTest(CL_FP_ROUND_TO_INF);
				PbitmapTest(CL_FP_FMA);
				PbitmapTest(CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT);
				PbitmapTest(CL_FP_SOFT_FLOAT);
				PbitmapEnd;

				PbitmapStart(device, CL_DEVICE_DOUBLE_FP_CONFIG);
				PbitmapTest(CL_FP_DENORM);
				PbitmapTest(CL_FP_INF_NAN);
				PbitmapTest(CL_FP_ROUND_TO_NEAREST);
				PbitmapTest(CL_FP_ROUND_TO_ZERO);
				PbitmapTest(CL_FP_ROUND_TO_INF);
				PbitmapTest(CL_FP_FMA);
				PbitmapTest(CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT);
				PbitmapTest(CL_FP_SOFT_FLOAT);
				PbitmapEnd;

				PconstStart(device, CL_DEVICE_GLOBAL_MEM_CACHE_TYPE);
				PconstTest(CL_NONE);
				PconstTest(CL_READ_ONLY_CACHE);
				PconstTest(CL_READ_WRITE_CACHE);
				PconstEnd;

				P(device, CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE);
				P(device, CL_DEVICE_GLOBAL_MEM_CACHE_SIZE);
				P(device, CL_DEVICE_GLOBAL_MEM_SIZE);
				P(device, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE);
				P(device, CL_DEVICE_MAX_CONSTANT_ARGS);

				PconstStart(device, CL_DEVICE_LOCAL_MEM_TYPE);
				PconstTest(CL_NONE);
				PconstTest(CL_LOCAL);
				PconstTest(CL_GLOBAL);
				PconstEnd;

				P(device, CL_DEVICE_LOCAL_MEM_SIZE);

				Pbool(device, CL_DEVICE_ERROR_CORRECTION_SUPPORT);

				//Pbool(device, CL_DEVICE_HOST_UNIFIED_MEMORY);
				P(device, CL_DEVICE_PROFILING_TIMER_RESOLUTION);

				Pbool(device, CL_DEVICE_ENDIAN_LITTLE);
				Pbool(device, CL_DEVICE_AVAILABLE);
				Pbool(device, CL_DEVICE_COMPILER_AVAILABLE);
				// P(device, CL_DEVICE_LINKER_AVAILABLE);

				PbitmapStart(device, CL_DEVICE_EXECUTION_CAPABILITIES);
				PbitmapTest(CL_EXEC_KERNEL);
				PbitmapTest(CL_EXEC_NATIVE_KERNEL);
				PbitmapEnd;

				PbitmapStart(device, CL_DEVICE_QUEUE_PROPERTIES);
				PbitmapTest(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
				PbitmapTest(CL_QUEUE_PROFILING_ENABLE);
				PbitmapEnd;

				// P(device, CL_DEVICE_BUILT_IN_KERNELS);
					//P(device, CL_DEVICE_PLATFORM);
				P(device, CL_DEVICE_NAME);
				P(device, CL_DEVICE_VENDOR);
				P(device, CL_DRIVER_VERSION);
				P(device, CL_DEVICE_PROFILE);
				P(device, CL_DEVICE_VERSION);
				P(device, CL_DEVICE_OPENCL_C_VERSION);
				P(device, CL_DEVICE_EXTENSIONS);

				// P(device, CL_DEVICE_PRINTF_BUFFER_SIZE);
				// Pbool(device, CL_DEVICE_PREFERRED_INTEROP_USER_SYNC);
				// P(device, CL_DEVICE_PARENT_DEVICE);
				// P(device, CL_DEVICE_PARTITION_MAX_SUB_DEVICES);
				// P(device, CL_DEVICE_PARTITION_PROPERTIES);
				// P(device, CL_DEVICE_PARTITION_AFFINITY_DOMAIN);
				// P(device, CL_DEVICE_PARTITION_TYPE);
				// P(device, CL_DEVICE_REFERENCE_COUNT);
			}
		}
	} catch (cl::Error err) {
		//std::cerr << "ERROR: " << err.what() << "(";
		//switch (err.err()) {
		//	case CL_INVALID_VALUE:       std::cerr << "CL_INVALID_VALUE"; break;
		//	case CL_INVALID_DEVICE_TYPE: std::cerr << "CL_INVALID_DEVICE_TYPE"; break;
		//	case CL_DEVICE_NOT_FOUND:    std::cerr << "CL_DEVICE_NOT_FOUND"; break;
		//	default:                     std::cerr << err.err(); break;
		//}
		//std::cerr << ")\n";
	}
	return ret;
}

std::string getGPUName()
{
	const auto platforms = getCLPlatformDevices();
	std::string device_name = "unknown";
	if (!platforms.empty())
		for (const auto &platform : platforms)
			for (const auto &[key, value] : platform.second) {
								// check if this is a GPU device, if so, get the device name and break
				if (key == "CL_DEVICE_TYPE" && value == "CL_DEVICE_TYPE_GPU") {
					for (const auto &[k, v] : platform.second) {
						if (k == "CL_DEVICE_NAME") {
							device_name = v;
							break;
						}
					}
					break;
				}
			}
	return device_name;
}
