/*
 *
 * Copyright (C) 2012 Fancy Younglove <<user@hostname.org>>
 * Copyright (C) 2012 CableLabs Inc
 *
 * File:    gstcldemux_stream.h
 * Author:  Fancy Younglove 2012
 */

#ifndef __GST_CLDEMUX_STREAM_H__
#define __GST_CLDEMUX_STREAM_H__

//=============================================================================
//  Constants
//=============================================================================
#define MAX_DEMUX_STREAMS 20

#define MAX_SECTION_SIZE 1024   // maximum size (private section can be 4096)

#define TRANSPORT_PACKET_SIZE 188

#define MAX_DESCRIPTORS  8
#define MAX_DESCRIPTOR_DATA 256

#define MAX_STREAMS 32

#define MAX_LOCATOR_STRING 256      // for EISS



/*=============================================================================
  Types
=============================================================================*/
struct Descriptor
{
  unsigned char tag;
  unsigned char length;
  unsigned char data[MAX_DESCRIPTOR_DATA];
};

struct Stream
{
  unsigned char type;
  unsigned short pid;
  unsigned short esLength;
  Descriptor descriptor;  // save the first one
};


//=============================================================================
//      class CldemuxListItem
//=============================================================================
class CldemuxListItem
{
public:
  CldemuxListItem()
  {
    next = previous = NULL;
  }

  virtual ~CldemuxListItem()
  {
  }

  CldemuxListItem *GetNext()
  {
    return next;
  }

  CldemuxListItem *GetPrevious()
  {
    return previous;
  }

  void SetNext(CldemuxListItem *n)
  {
    next = n;
  }

  void SetPrevious(CldemuxListItem *p)
  {
    previous = p;
  }

  virtual void DebugPrint(unsigned char indent=0)
  {
  }

private:
  // Pointers to the adjacent items of this CldemuxListItem. This object does
  // not own the memory pointed at by these:
  CldemuxListItem *next;
  CldemuxListItem *previous;
};


//=============================================================================
//      class CldemuxList
//=============================================================================
class CldemuxList
{
public:
  CldemuxList()
  {
    first = last = NULL;
  }

  ~CldemuxList();

  CldemuxListItem * AddLast(CldemuxListItem *item);

  // Print debug info about all items in the list:
  void DebugPrint(unsigned char indent=0);

  CldemuxListItem * AddFirst(CldemuxListItem *item);

  bool Remove(CldemuxListItem *toBeRemoved);

  CldemuxListItem *GetFirst()
  {
    return first;
  }

  CldemuxListItem *GetLast()
  {
    return last;
  }

private:
  CldemuxListItem *first;
  CldemuxListItem *last;
};


//=============================================================================
//      class MpegPAT
//=============================================================================
class MpegSection
{
public:

private:

};


//=============================================================================
//  class CldemuxStream
//=============================================================================
class CldemuxStream
{
public:
  CldemuxStream();

  CldemuxStream(int p, int psi);

  virtual ~CldemuxStream();

  void init()
  {
    pid = 0;
    packetCount = 0;
    psiStream = 0;
    sectionIndex = 0;
    packetPtr = NULL;
    debugPrintCount = 0;

    initHeaders();
  }

  void initHeaders()
  {
    continuity = 0;
    scrambling = 0;
    adaptation = 0;
    pointer = 0;
    tableId = 0;
    sectionLength = 0;
    transportStreamId = 0;
    versionNumber = 0;
    currentNext = 0;
    sectionNumber = 0;
    lastSectionNumber = 0;
    sectionHeaderIndex = 0;
  }

  int getPid()
  {
    return pid;
  }

  int getPacketCount()
  {
    return packetCount;
  }

  void setPsiStream(int psi)
  {
    psiStream = psi;
  }

  int getPsiStream()
  {
    return psiStream;
  }

  unsigned char getTransportByte()
  {
    unsigned char byte = packetPtr[transportBytesRead];
    if (transportBytesRead < TRANSPORT_PACKET_SIZE-1)
    {
      transportBytesRead++;
    }

    return byte;
  }

  unsigned short getTransportShort()
  {
    unsigned short value;

    value = getTransportByte();
    value <<= 8;

    value += getTransportByte();

    return value;
  }

  unsigned char getSectionByte()
  {
    unsigned char byte = sectionBuffer[sectionBytesRead];
    if (sectionBytesRead < MAX_SECTION_SIZE-1)
    {
      sectionBytesRead++;
    }

    return byte;
  }

  unsigned short getSectionShort()
  {
    unsigned short value;

    value = getSectionByte();
    value <<= 8;

    value += getSectionByte();

    return value;
  }

  int  parseSectionHeader(int readTSI);
  void parseSection();
  void parsePAT();
  void parsePMT();
  void parseEISS();

  void parseDescriptors(int length);
  int  parseDescriptor();
  void parseStreamInfo(int length);
  int  parseStream();

  const char * getDescriptorString(int tag);
  const char * getStreamString(int type);
  const char * getAppCcString(int type);
  const char * getProtocolString(int type);

  void processPacket(unsigned short packet_payload_start, unsigned char *buffer);

  unsigned short getShort(unsigned char *buffer, int index)
  {
    unsigned short data = buffer[index];
    data <<= 8;
    data += buffer[index+1];
    return data;
  }

  //--- static member functions
  static void addStream(int pid, int psi);
  static void initialize();
  static void finalize();
  static void processAllPackets(unsigned char *buffer);


private:
  int pid;            // the PID used by this stream
  int packetCount;    // # packets processed for this pid
  int psiStream;      // =0 do not attempt to assemble PSI tables
                      // =1 assemble sections/tables such as PAT, PMT
  int debugPrintCount;

  // section buffer - used to hold data to assemble 1 section
  unsigned char sectionBuffer[MAX_SECTION_SIZE];
  int sectionBytesRead;      // index within current section, when parsing
  int sectionIndex;          // index within current section, when assembling

  // ptr to data for the current transport packet
  unsigned char *packetPtr;
  int transportBytesRead;    // index within current transport packet

  // data from the most recent transport packet header
  unsigned char continuity;
  unsigned char scrambling;
  unsigned char adaptation;
  unsigned char pointer;

  // data from the section header. This data is common to all tables such
  // as PAT, PMT, etc
  unsigned char tableId;
  unsigned short sectionLength;
  unsigned short transportStreamId;
  unsigned char versionNumber;
  unsigned char currentNext;
  unsigned char sectionNumber;
  unsigned char lastSectionNumber;
  int sectionHeaderIndex;

  // Other section data
  int crc32;

  // PMT data
  unsigned short programNumber;
  unsigned short pmtPid;
  Descriptor descriptors[MAX_DESCRIPTORS];
  int numDescriptors;
  int numStreams;
  Stream streams[MAX_STREAMS];

  //--- static data
  static int streamIndex;

  static CldemuxStream *streamArray[MAX_DEMUX_STREAMS];
};

#endif /* __GST_CLDEMUX_STREAM_H__ */













