#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "mp4v2/mp4v2.h"


#define ENABLE_VIDEO 	1
#define ENABLE_AUDIO 	1


// 编译时Makefile里控制
#ifdef ENABLE_DEBUG
	#define DEBUG(fmt, args...) 	printf(fmt, ##args)
#else
	#define DEBUG(fmt, args...)
#endif


#define TIMESCALE 	90000
#define BUF_SIZE 	1*1024*1024


/* 从H264文件中获取一个NALU的数据
 * fpVideo: 		[in]  h264视频文件的句柄
 * pNaluData: 		[out] 函数内部会填充一个完整的NALU数据到该地址
 * startCodeLen: 	[out] 该NALU的开始码长度，“00 00 01”为3， “00 00 00 01”为4
 * 返回值: 			该NALU的整个数据长度（开始码+数据）、-1出错或文件已结束
 */
static int getNALU(FILE *fpVideo, unsigned char *pNaluData, int *startCodeLen);


/* 从ADTS格式的AAC文件中获取一帧音频数据
 * fpAudio: 		[in]  aac音频文件的句柄
 * pAdtsFrameData： [out] 函数内部会填充一帧完整的ADTS数据
 * 返回值: 			该ADTS帧的总长度（头部+数据）、-1出错或文件已结束
 */
static int getAdtsFrame(FILE *fpAudio, unsigned char *pAdtsFrameData);

/* 参考网页自己实现的一个用于填充解码器的函数，用于`MP4SetTrackESConfiguration`传递参数
 * 返回值: 16bit(le)用于解码的信息
 */
static short getAudioConfig(unsigned int sampleRate, unsigned int channels);

void print_usage(const char *process)
{
	printf("\033[33m(Note: Only support H.264 and AAC(LC) in this demo.)\033[0m\n"
		   "examples: \n"
		   "    %s -h\n"
		   "    %s --help\n"
		   "    %s -a ./avfile/test1_44100_stereo.aac -r 44100 -c 2 -v ./avfile/test1_856x480_24fps.h264 -W 856 -H 480 -f 24 -o ./test1_out.mp4\n"
		   "    %s --audio_file=./avfile/test2_44100_mono.aac --audio_samplerate=44100 --audio_channels=1 "
		   "--video_file=./avfile/test2_640x360_20fps.h264 --video_width=640 --video_height=360 --video_fps=20 --output_mp4=./test2_out.mp4\n",
		   process, process, process, process);
}


int main(int argc, char *argv[])
{
	/* 输入/输出文件 */
	FILE *fpVideo = NULL;
	FILE *fpAudio = NULL;
	char audioFileName[128] = {0};
	char videoFileName[128] = {0};
	char mp4FileName[128] = {0};
	unsigned int audio_samplerate = 0;
	unsigned int audio_channels = 0;
	unsigned int video_width = 0;
	unsigned int video_height = 0;
	unsigned int video_fps = 0;
	unsigned char *pBuf = (unsigned char *)malloc(BUF_SIZE);
	MP4FileHandle mp4Handler = 0;
	MP4TrackId videoTrackId = 0;
	MP4TrackId audioTrackId = 0;
	short audioConfig = 0;

	/* 判断输入参数 */
	if(argc == 1)
	{
		print_usage(argv[0]);
		return -1;
	}

	/* 解析命令行参数（注意：其实音视频的头部本身也带有参数说明，但我们已经知道参数，所以自己填充） */
	char option = 0;
	int option_index = 0;
	const char *short_options = "ha:r:c:v:W:H:f:o:";
	struct option long_options[] =
	{
		{"help",            no_argument,       NULL, 'h'},
		{"audio_file",      required_argument, NULL, 'a'},
		{"audio_samplerate",required_argument, NULL, 'r'},
		{"audio_channels",  required_argument, NULL, 'c'},
		{"video_file",	    required_argument, NULL, 'v'},
		{"video_width",	    required_argument, NULL, 'W'},
		{"video_height",	required_argument, NULL, 'H'},
		{"video_fps",	    required_argument, NULL, 'f'},
		{"output_mp4",      required_argument, NULL, 'o'},
		{NULL,              0,                 NULL,  0 },
	};
	while((option = getopt_long_only(argc, argv, short_options, long_options, &option_index)) != -1)
	{
		switch(option)
		{
			case 'h':
				print_usage(argv[0]);
				return 0;
			case 'a':
				strncpy(audioFileName, optarg, 128);
				break;
			case 'r':
				audio_samplerate = atoi(optarg);
				break;
			case 'c':
				audio_channels = atoi(optarg);
				break;
			case 'v':
				strncpy(videoFileName, optarg, 128);
				break;
			case 'W':
				video_width = atoi(optarg);
				break;
			case 'H':
				video_height = atoi(optarg);
				break;
			case 'f':
				video_fps = atoi(optarg);
				break;
			case 'o':
				strncpy(mp4FileName, optarg, 128);
				break;
			defalut:
				printf("Unknown argument!\n");
				break;
		}
 	}
	
	if(!audio_samplerate || !audio_channels || !video_width || !video_height || !video_fps ||\
	   !strlen(audioFileName) || !strlen(videoFileName) || !strlen(mp4FileName))
	{
		printf("Parameter not set!\n");
		print_usage(argv[0]);
		return -1;
	}
	else
	{
		printf("\n**************************************\n"
			   "input: \n"
			   "\t audio file name: %s\n"
			   "\t  - sample rate: %d Hz\n"
			   "\t  - channels: %d\n"
			   "\t video file name: %s\n"
			   "\t  - width: %d\n"
			   "\t  - height: %d\n"
			   "\t  - fps: %d\n"
			   "output: \n"
			   "\t file name: %s\n"
			   "**************************************\n\n",
			  audioFileName, audio_samplerate, audio_channels, 
			  videoFileName, video_width, video_height, video_fps, mp4FileName);
	}

	// 文件操作
	fpAudio = fopen(audioFileName, "rb");
	fpVideo = fopen(videoFileName, "rb");
	if (fpAudio == NULL || fpVideo == NULL)
	{
		printf("open audio or video file error!\n");
		goto error_exit2;
	}


	/* MP4操作 1/8：创建mp4文件 */
	//mp4Handler = MP4Create(mp4FileName, 0);
	mp4Handler = MP4CreateEx(mp4FileName, 0, 1, 1, 0, 0, 0, 0);
	if (mp4Handler == MP4_INVALID_FILE_HANDLE)
	{
		printf("create mp4 file error!\n");
		goto error_exit2;
	}

	/* MP4操作 2/8：设置TimeScale */
	MP4SetTimeScale(mp4Handler, TIMESCALE);


#if ENABLE_VIDEO
	/* MP4操作 3/8：添加H264视频track，设置视频格式，本程序是在已知SPS的一些数值时直接显式填充 */
	videoTrackId = MP4AddH264VideoTrack(mp4Handler, TIMESCALE, TIMESCALE/video_fps,
										video_width, video_height,
										0x64, // sps[1] AVCProfileIndication
										0x00, // sps[2] profile_compat
										0x1F, // sps[3] AVCLevelIndication
										3);   // 4 bytes length before each NAL unit
	if (videoTrackId == MP4_INVALID_TRACK_ID)
	{
		printf("add h264 video track error!\n");
		goto error_exit1;
	}
	MP4SetVideoProfileLevel(mp4Handler, 0x7F);
#endif


#if ENABLE_AUDIO
	/* MP4操作 4/8：添加音频track，设置音频格式 */
	audioTrackId = MP4AddAudioTrack(mp4Handler, audio_samplerate, 1024,
											MP4_MPEG4_AUDIO_TYPE);
	if (audioTrackId == MP4_INVALID_TRACK_ID)
	{
		printf("add aac audio track error!\n");
		goto error_exit1;
	}
	MP4SetAudioProfileLevel(mp4Handler, 0x2); // 0x02 ==> MPEG4 AAC LC

	/* MP4操作 5/8：根据音频协议、采样率、声道设置音频参数 */
	// 推荐都填充上，否则部分播放器播放时没有声音，配置参数有两种方式获取：
	//  - 从开源项目faac的`faacEncGetDecoderSpecificInfo`函数获取；
	//  - 我们自己实现了一个，这样可以避免依赖于其他项目的程序代码。<=
	audioConfig = getAudioConfig(audio_samplerate, audio_channels);
	audioConfig = ((audioConfig & 0x00ff) << 8) | ((audioConfig >> 8) & 0x00ff);
	DEBUG("\n[audioConfig: 0x%04x]\n\n", audioConfig);
	MP4SetTrackESConfiguration(mp4Handler, audioTrackId, (const uint8_t*)&audioConfig, 2);
#endif


#if ENABLE_VIDEO
	/* MP4操作 6/8：写视频 */
	while(1)
	{
		int startCodeLen = 0;
		int naluLen = 0;
		int naluDataLen = 0;
		int naluType = 0;
		unsigned char *pNaluData = NULL;
		static unsigned int frameIndex = 0; // 从0开始算起

		naluLen = getNALU(fpVideo, pBuf, &startCodeLen);
		if(naluLen <= 0)
			break;

		DEBUG("[\033[35mvideo\033[0m] start code: ");
		for(int i = 0; i < startCodeLen; i++)
			DEBUG("%02x ", pBuf[i]);
		DEBUG("\t size: %d\t NALU type(%02x): ", naluLen, pBuf[startCodeLen]);

		pNaluData = pBuf + startCodeLen; 	  // start code后边的NALU数据
		naluDataLen = naluLen - startCodeLen; // NALU数据长度
		naluType  = pNaluData[0] & 0x1f; 	  // NALU类型
		
		switch(naluType)
		{
			case 0x06: // SEI
				// 不是必须的，暂不处理
				DEBUG("SEI <Do not pack to mp4 file in this demo, skip it!>\n");
				break;
			case 0x07: // SPS
				DEBUG("SPS\n");
				MP4AddH264SequenceParameterSet(mp4Handler, videoTrackId, pNaluData, naluDataLen);
				break;
			case 0x08: // PPS
				DEBUG("PPS\n");
				MP4AddH264PictureParameterSet(mp4Handler, videoTrackId, pNaluData, naluDataLen);
				break;
			case 0x05: // IDR
				/* 注：这里处理的默认是4字节的开始码，应考虑3字节还是4字节的情况 */
				DEBUG("IDR\t frame index: %d\n", frameIndex++);
				pBuf[0] = (naluDataLen >> 24) & 0xFF;
				pBuf[1] = (naluDataLen >> 16) & 0xFF;
				pBuf[2] = (naluDataLen >> 8) & 0xFF;
				pBuf[3] = (naluDataLen >> 0) & 0xFF;
				MP4WriteSample(mp4Handler, videoTrackId, pBuf, naluLen, MP4_INVALID_DURATION, 0, 1); // 最后一个参数: isSyncSample
				break;
			case 0x01: // SLICE
				/* 注：这里处理的默认是4字节的开始码，应考虑3字节还是4字节的情况 */
				DEBUG("SLICE\t frame index: %d\n", frameIndex++);
				pBuf[0] = (naluDataLen >> 24) & 0xFF;
				pBuf[1] = (naluDataLen >> 16) & 0xFF;
				pBuf[2] = (naluDataLen >> 8) & 0xFF;
				pBuf[3] = (naluDataLen >> 0) & 0xFF;
				MP4WriteSample(mp4Handler, videoTrackId, pBuf, naluLen, MP4_INVALID_DURATION, 0, 0);
				break;
			default:
				DEBUG("Other NALU type <Do not pack to mp4 file in this demo, skip it!>\n");
				break;
		}
	}
#endif


#if ENABLE_AUDIO
	/* MP4操作 7/8：写音频 */
	while(1)
	{
		int aacFrameLen = 0;
		static unsigned int frameIndex = 0;

		aacFrameLen = getAdtsFrame(fpAudio, pBuf);
		if(aacFrameLen <= 0)
		{
			break;
		}
		DEBUG("[\033[36maudio\033[0m] frame index: %d\t size: %d\n", frameIndex++, aacFrameLen);

		// 写入音频数据时不需要带上ADTS的头部，所以要偏移7个字节的头部
		MP4WriteSample(mp4Handler, audioTrackId, pBuf+7, aacFrameLen-7, MP4_INVALID_DURATION, 0, 1);
	}
#endif

error_exit1:
	/* MP4操作 8/8：关闭mp4文件 */
	MP4Close(mp4Handler, 0);

error_exit2:
	fclose(fpAudio);
	fclose(fpVideo);
	free(pBuf);

	printf("\033[32mSuccess!\033[0m\n");

	return 0;
}



static int getNALU(FILE *fpVideo, unsigned char *pNaluData, int *startCodeLen)
{
	int readBytes = 0;
	unsigned int pos = 0;

	if(!fpVideo)
		return -1;

	if((readBytes = fread(pNaluData, 1, 4, fpVideo)) <= 0)
		return -1;

	// 判断NALU的`start code`类型
	if(pNaluData[0] == 0 && pNaluData[1] == 0 && pNaluData[2] == 0 && pNaluData[3] == 1)
	{
		pos = 4; // 从pNaluData[4]开始存储剩余的数据
		*startCodeLen = 4;
	}
	else if(pNaluData[0] == 0 && pNaluData[1] == 0 && pNaluData[2] == 1)
	{
		pos = 3;
		*startCodeLen = 3;
		fseek(fpVideo, -1, SEEK_CUR); // 调整一下文件指针
	}


	// 查找下一个NALU
	while(1)
	{
		int val = 0;
		if((val = fgetc(fpVideo)) != EOF)
		{
			pNaluData[pos] = (unsigned char)val;
		}
		else
		{
			// 文件已结束，上一轮的循环末尾pos不应该被加1
			pos -= 1;
			break;
		}

		// 判断“00 00 00 01”和“00 00 01”两种情况，必须先判断“00 00 00 01”，因为它包含了“00 00 01”这种情况
		if(pNaluData[pos-3] == 0 && pNaluData[pos-2] == 0 && pNaluData[pos-1] == 0 && pNaluData[pos] == 1)
		{
			fseek(fpVideo, -4, SEEK_CUR);
			pos -= 4;
			break;
		}
		else if(pNaluData[pos-2] == 0 && pNaluData[pos-1] == 0 && pNaluData[pos] == 1)
		{
			fseek(fpVideo, -3, SEEK_CUR);
			pos -= 3;
			break;
		}
		pos++;
	}

	// 返回的是一个NALU的长度（数组下标 + 1 = 一个NALU的长度）
	return pos+1;
}

static int getAdtsFrame(FILE *fpAudio, unsigned char *pAdtsFrameData)
{
	int readBytes = 0;
	unsigned int adtsFrameLen = 0;

	if(!fpAudio)
		return -1;

	// ADTS的头部大小是7Bytes，详细看本文件底下说明
	readBytes = fread(pAdtsFrameData, 1, 7, fpAudio);
	if(readBytes <= 0)
	{
		return -1;
	}

	// 计算一帧ADTS的长度
	adtsFrameLen = (((unsigned int)pAdtsFrameData[3] & 0x03) << 11) | 
					(((unsigned int)pAdtsFrameData[4] & 0xFF) << 3) |
					(((unsigned int)pAdtsFrameData[5] & 0xE0) >> 5);

	// 读取7字节头部外的剩余的一帧ADTS数据
	readBytes = fread(pAdtsFrameData+7, 1, adtsFrameLen-7, fpAudio); // 偏移头部的7个字节继续写入
	if(readBytes <= 0)
	{
		return -1;
	}

	return adtsFrameLen;
}

static int GetSRIndex(unsigned int sampleRate)
{
   if (92017 <= sampleRate)return 0;
   if (75132 <= sampleRate)return 1;
   if (55426 <= sampleRate)return 2;
   if (46009 <= sampleRate)return 3;
   if (37566 <= sampleRate)return 4;
   if (27713 <= sampleRate)return 5;
   if (23004 <= sampleRate)return 6;
   if (18783 <= sampleRate)return 7;
   if (13856 <= sampleRate)return 8;
   if (11502 <= sampleRate)return 9;
   if (9391 <= sampleRate)return 10;

   return 11;
}

static short getAudioConfig(unsigned int sampleRate, unsigned int channels)
{
	/* 参考: https://my.oschina.net/u/1177171/blog/494369
	 * [5 bits AAC Object Type] [4 bits Sample Rate Index] [4 bits Channel Number] [3 bits 0]
	 */

#define	MAIN 1
#define	LOW  2
#define	SSR  3
#define	LTP  4

	return (LOW << 11) | (GetSRIndex(sampleRate) << 7) | (channels << 3);
}


#if 0
/* ADTS的头部，大小为7个字节，这里对每个元素都用`unsigned int`来表示，所以需要自行填充数据后才能使用 */
struct AdtsHeader
{
    unsigned int syncword;  		//12 bit 同步字 '1111 1111 1111'，说明一个ADTS帧的开始
    unsigned int id;        		//1 bit MPEG 标示符， 0 for MPEG-4，1 for MPEG-2
    unsigned int layer;     		//2 bit 总是'00'
    unsigned int protectionAbsent;  //1 bit 1表示没有crc，0表示有crc
    unsigned int profile;           //1 bit 表示使用哪个级别的AAC
    unsigned int samplingFreqIndex; //4 bit 表示使用的采样频率
    unsigned int privateBit;        //1 bit
    unsigned int channelCfg; 		//3 bit 表示声道数
    unsigned int originalCopy;      //1 bit 
    unsigned int home;              //1 bit 

    /*下面的为改变的参数即每一帧都不同*/
    unsigned int copyrightIdentificationBit;   //1 bit
    unsigned int copyrightIdentificationStart; //1 bit
    unsigned int aacFrameLength;               //13 bit 一个ADTS帧的长度包括ADTS头和AAC原始流
    unsigned int adtsBufferFullness;           //11 bit 0x7FF 说明是码率可变的码流

    /* number_of_raw_data_blocks_in_frame
     * 表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧
     * 所以说number_of_raw_data_blocks_in_frame == 0 
     * 表示说ADTS帧中有一个AAC数据块并不是说没有。(一个AAC原始帧包含一段时间内1024个采样及相关数据)
     */
    unsigned int numberOfRawDataBlockInFrame; //2 bit
};

#endif

