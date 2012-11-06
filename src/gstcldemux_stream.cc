/*
 *
 * Copyright (C) 2012 Fancy Younglove <<user@hostname.org>>
 * Copyright (C) 2012 CableLabs Inc
 *
 * File:    gstcldemux_stream.cpp
 * Author:  Fancy Younglove 2012
 */


//=============================================================================
//  Includes
//=============================================================================
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include <ctype.h>

#include "gstcldemux_stream.hh"


//=============================================================================
//  Constants
//=============================================================================
#define MAX_PID  8192



//=============================================================================
//  Data
//=============================================================================
int CldemuxStream::streamIndex;

unsigned int totalPacketCount = 0;

CldemuxStream * CldemuxStream::streamArray[MAX_DEMUX_STREAMS];


extern "C"
{
  void cldemux_stream_initialize(void)
  {
    CldemuxStream::initialize();
  }

  void cldemux_stream_addStream(int pid, int psi)
  {
    //g_print("cldemux_stream_addStream pid=%d\n", pid);
    CldemuxStream::addStream(pid, psi);
  }

  void cldemux_stream_finalize(void)
  {
    CldemuxStream::finalize();
  }

  void cldemux_stream_process_packet(unsigned char *buffer)
  {
    CldemuxStream::processAllPackets(buffer);
  }
}


//=============================================================================
//  CldemuxStream::initialize()
//=============================================================================
void CldemuxStream::initialize()
{
  g_print("-------------- gstcldemux_stream initialize -------------\n");

  for (int i=0; i<MAX_DEMUX_STREAMS; i++)
  {
    streamArray[i] = NULL;
  }

  // Add the first stream for the PAT
  addStream(0, 1);       // PAT's pid always is 0

  streamIndex = 1;
}


//=============================================================================
//  CldemuxStream::finalize()
//=============================================================================
void CldemuxStream::finalize()
{
  g_print("-------------- gstcldemux_stream finalize totalPacketCount=%d -------------\n", totalPacketCount);

  for (int i=0; i<MAX_DEMUX_STREAMS; i++)
  {
    if (streamArray[i] != NULL)
    {
      delete streamArray[i];
      streamArray[i] = NULL;
    }
  }
}


//=============================================================================
//  CldemuxStream::CldemuxStream()
//=============================================================================
CldemuxStream::CldemuxStream()
{
  g_print(">>>> CldemuxStream CONSTRUCTOR no pid\n");
  init();
}


//=============================================================================
//  CldemuxStream::CldemuxStream()
//=============================================================================
CldemuxStream::CldemuxStream(int p, int psi)
{
  g_print(">>>> CldemuxStream CONSTRUCTOR pid=%d  psi=%d\n", p, psi);
  init();
  pid = p;

  if ((pid == 0) || (psi != 0))
  {
    psiStream = 1;
  }
}


//=============================================================================
//  CldemuxStream::~CldemuxStream()
//=============================================================================
CldemuxStream::~CldemuxStream()
{
  g_print("<<<< CldemuxStream DESTRUCTOR pid=%d  packetCount=%d\n", pid, packetCount);
}


//=============================================================================
//  CldemuxStream::addStream()
//=============================================================================
void CldemuxStream::addStream(int pid, int psi)
{
  //g_print("CldemuxStream::addStream pid=%d\n", pid);

  for (int i=0; i<MAX_DEMUX_STREAMS; i++)
  {
    if (streamArray[i] && (streamArray[i]->getPid() == pid))
    {
      //g_print("addStream i=%d pid=%d\n", i, pid);

      return;   // already have a stream with this pid
    }
  }

  //g_print("streamIndex=%d\n", streamIndex);

  // Create new stream for that pid
  if (streamIndex < MAX_DEMUX_STREAMS-1)
  {
    if (streamArray[streamIndex] == NULL)
    {
      streamArray[streamIndex++] = new CldemuxStream(pid, psi);
    }
  }
}


//=============================================================================
//  CldemuxStream::processAllPackets()
//  This is the static function that passes to the correct instance with a
//  matching pid.
//=============================================================================
void CldemuxStream::processAllPackets(unsigned char *buffer)
{
  unsigned short pid;
  unsigned short packet_error;
  unsigned short packet_payload_start;
  unsigned short packet_priority;

  totalPacketCount++;

  pid = buffer[1];
  pid <<= 8;
  pid += buffer[2];

  packet_error         = (pid & 0x8000) ? 1 : 0;
  packet_payload_start = (pid & 0x4000) ? 1 : 0;
  packet_priority      = (pid & 0x2000) ? 1 : 0;

  pid &= 0x1FFF;

  // TBD: faster way to find matching stream
  for (int i=0; i<MAX_DEMUX_STREAMS; i++)
  {
    if (streamArray[i] && streamArray[i]->getPid() == pid)
    {
      streamArray[i]->processPacket(packet_payload_start, buffer);
      return;
    }
  }
}


//=============================================================================
//  CldemuxStream::processPacket()
//  Instance function to process a single TS packet.
//=============================================================================
void CldemuxStream::processPacket(unsigned short packet_payload_start, unsigned char *buffer)
{
  int i;
  int bytesToCopy;

  packetCount++;
  packetPtr = buffer;
  transportBytesRead = 3;            // skip over SYNC and pid

  if (psiStream == 0)
  {
    // No further processing for video/audio streams
    return;
  }

  if (packet_payload_start)
  {
    // This is the first transport packet of a section. Others may follow.

    // Get the transport and section headers
    continuity = getTransportByte();

    scrambling = continuity;
    scrambling >>= 6;
    scrambling &= 0x03;

    adaptation = continuity;
    adaptation >>= 4;
    adaptation &= 0x03;

    continuity &= 0x0F;

    if (adaptation != 1)
    {
      // Adaptation = 0, reserved
      // Adaptation = 1, payload only
      // Adaptation = 2, adaptation only
      // Adaptation = 3, adaptation, then payload

      return;  // TBD process adaptation
    }

    pointer = getTransportByte();

    sectionHeaderIndex = 5 + pointer;

    if (sectionHeaderIndex > TRANSPORT_PACKET_SIZE-3)
    {
      return;     // invalid value for pointer
    }

    // The section header starts after pointer bytes
    transportBytesRead = sectionHeaderIndex;

    tableId = getTransportByte();

    sectionLength = getTransportShort();

    sectionLength &= 0x0FFF;

    bytesToCopy = sectionLength + 3;  // includes the tableId & length

    //g_print("bytesToCopy=%d sectionHeaderIndex=%d pointer=%d\n", bytesToCopy, sectionHeaderIndex, pointer);

    // Copy data from transport packet to section buffer
    for (i=sectionHeaderIndex, sectionIndex=0;
        (i<TRANSPORT_PACKET_SIZE) && (sectionIndex < MAX_SECTION_SIZE) && (sectionIndex < bytesToCopy);
        i++, sectionIndex++)
    {
      sectionBuffer[sectionIndex] = buffer[i];
    }

    // TODO: check to make sure all packets assembled, then parse
    parseSection();
  }
  else
  {
    // This transport packet contains data in the middle of a section, append to
    // a section in progress.

    // ToDO: handle for sections which are larger than 1 transport stream packet.
  }
}


//=============================================================================
//  CldemuxStream::parseSection()
//=============================================================================
void CldemuxStream::parseSection()
{
  sectionBytesRead = 0;

  if (tableId == 0)
  {
    parsePAT();
  }
  else if (tableId == 2)
  {
    parsePMT();
  }
  else if ((tableId == 0xE0) || (tableId == 0xE2))
  {
    parseEISS();
  }
}


//=============================================================================
//  CldemuxStream::parseSectionHeader()
//=============================================================================
int CldemuxStream::parseSectionHeader(int readTSI)
{
  unsigned char byte;
  int bytesRead = 0;

  getSectionByte();           // tableId already parsed
  getSectionShort();          // section length already parsed
  bytesRead = 3;

  if (readTSI)
  {
    transportStreamId = getSectionShort();
    versionNumber     = getSectionByte();
    bytesRead += 3;

    currentNext = versionNumber & 0x01;
    versionNumber &= 0x3F;
    versionNumber >>= 1;
  }
  else
  {
    // For EISS table
    byte = getSectionByte();      // reserved
    bytesRead++;
  }

  sectionNumber = getSectionByte();
  lastSectionNumber = getSectionByte();
  bytesRead += 2;

  return bytesRead;
}


//=============================================================================
//  CldemuxStream::parsePAT()
//=============================================================================
void CldemuxStream::parsePAT()
{
  int i;
  int numPids;

  parseSectionHeader(1);

  // Read the program number / network or PMT PIDs
  numPids = (sectionLength - 9) / 4;

  // Todo: save a list of PMT pids. For now, just use the last one
  for (i=0; i<numPids; i++)
  {
    programNumber = getSectionShort();
    pmtPid        = getSectionShort();

    pmtPid &= 0x1FFF;
  }

  crc32 = getSectionShort();



  if (debugPrintCount < 10)
  {
    debugPrintCount++;

    //g_print("PAT tableId=%d length=%d\n", tableId, sectionLength);

    g_print("\n");
    g_print("========= PAT packet: ======\n");
    g_print("       scrambling=%d\n", scrambling);
    g_print("       adaptation=%d\n", adaptation);
    g_print("       continuity=%d\n", continuity);
    g_print("--------------------\n");
    g_print("          tableId=%d\n", tableId);
    g_print("    sectionLength=%d\n", sectionLength);
    g_print("transportStreamId=%d\n", transportStreamId);
    g_print("    versionNumber=%d\n", versionNumber);
    g_print("    sectionNumber=%d\n", sectionNumber);
    g_print("lastSectionNumber=%d\n", lastSectionNumber);
    g_print("    programNumber=%d\n", programNumber);
    g_print("           pmtPid=%d\n", pmtPid);

    if (pmtPid != 0)
    {
      cldemux_stream_addStream(pmtPid, 1);
    }
  }
}


//=============================================================================
//  CldemuxStream::parsePMT()
//=============================================================================
void CldemuxStream::parsePMT()
{
  int bytesRead;        // should be 188 when the entire packet is read
  unsigned short pcrPid;
  unsigned short programInfoLength;
  int i, j;
  unsigned char byte;
  unsigned short streamInfoLength;


  parseSectionHeader(1);

  pcrPid = getSectionShort();
  pcrPid &= 0x1FFF;

  programInfoLength = getSectionShort();
  programInfoLength &= 0xFFF;

  // Read descriptors
  parseDescriptors(programInfoLength);

  // Calculate length of stream info, subtract 9 for header bytes after the section length
  // subtract 4 for the CRC
  streamInfoLength = sectionLength - 9 - programInfoLength - 4;

  parseStreamInfo(streamInfoLength);


  if (debugPrintCount < 10)
  {
    debugPrintCount++;

    g_print("\n");
    g_print("=========== PMT packet: ======\n");
    g_print("         scrambling=%d\n", scrambling);
    g_print("         adaptation=%d\n", adaptation);
    g_print("         continuity=%d\n", continuity);
    g_print("----------------------\n");
    g_print("            tableId=%d\n", tableId);
    g_print("      sectionLength=%d\n", sectionLength);
    g_print("  transportStreamId=%d\n", transportStreamId);
    g_print("      versionNumber=%d\n", versionNumber);
    g_print("      sectionNumber=%d\n", sectionNumber);
    g_print("  lastSectionNumber=%d\n", lastSectionNumber);
    g_print("            pcr_pid=%d\n", pcrPid);
    g_print("  programInfoLength=%d\n", programInfoLength);
    g_print("   streamInfoLength=%d\n", streamInfoLength);

    for (i=0; i<numDescriptors; i++)
    {
      g_print("  descriptor[%d] tag=%d(%s) length=%d\n",
              i,
              descriptors[i].tag,
              getDescriptorString(descriptors[i].tag),
              descriptors[i].length);

      if (descriptors[i].length > 16)
      {
        descriptors[i].length = 16;    // limit # bytes displayed
      }

      g_print("---> Data: ");
      for (j=0; j<descriptors[i].length; j++)
      {
        g_print("%.2x[%c] ", descriptors[i].data[j], isprint(descriptors[i].data[j]) ? descriptors[i].data[j] : '.' );
      }
      g_print("\n");
    }
    g_print("Note: find registration descriptors at: http://www.smpte-ra.org/mpegreg/mpegreg.html\n");

    for (i=0; i<numStreams; i++)
    {
      g_print("  stream[%d] type=%d(%s) pid=%d esLength=%d desc tag=%d desc len=%d\n",
             i,
             streams[i].type,
             getStreamString(streams[i].type),
             streams[i].pid,
             streams[i].esLength,
             streams[i].descriptor.tag,
             streams[i].descriptor.length);


      g_print("  ---> Data: ");
      for (j=0; j<streams[i].descriptor.length; j++)
      {
        g_print("%.2x[%c] ", streams[i].descriptor.data[j],
            isprint(streams[i].descriptor.data[j]) ? streams[i].descriptor.data[j] : '.' );
      }
      g_print("\n");

      if (streams[i].type == 0xC0)
      {
        cldemux_stream_addStream(streams[i].pid, 1);
      }
      else
      {
        cldemux_stream_addStream(streams[i].pid, 0);    // audio, video, or don't care
      }

    }

    g_print("\n");

  }
}


//=============================================================================
//  CldemuxStream::getStreamString()
//=============================================================================
const char * CldemuxStream::getStreamString(int type)
{
  const char *str;

  switch (type)
  {
   case 0x00: str="ITU-T | ISO/IEC Reserved"; break;
   case 0x01: str="ISO/IEC 11172 Video"; break;
   case 0x02: str="ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream"; break;
   case 0x03: str="ISO/IEC 11172 Audio"; break;
   case 0x04: str="ISO/IEC 13818-3 Audio"; break;
   case 0x05: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections"; break;
   case 0x06: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data"; break;
   case 0x07: str="ISO/IEC 13522 MHEG"; break;
   case 0x08: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC"; break;
   case 0x09: str="ITU-T Rec. H.222.1"; break;
   case 0x0A: str="ISO/IEC 13818-6 type A"; break;
   case 0x0B: str="ISO/IEC 13818-6 type B"; break;
   case 0x0C: str="ISO/IEC 13818-6 type C"; break;
   case 0x0D: str="ISO/IEC 13818-6 type D"; break;
   case 0x0E: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary"; break;
   case 0x0F: str="ISO/IEC 13818-7 Audio with ADTS transport syntax"; break;
   case 0x10: str="ISO/IEC 14496-2 Visual"; break;
   case 0x11: str="ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1"; break;
   case 0x12: str="ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets"; break;
   case 0x13: str="ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC14496_sections."; break;
   case 0x14: str="ISO/IEC 13818-6 Synchronized Download Protocol"; break;

   default:
     if (type == 129)
     {
       str="ATSC AC-3 Audio";
     }
     else if (type == 0x86)
     {
       str="SCTE-35 splice information table";
     }
     else if (type == 0xC0)
     {
       str = "EISS ?";
     }
     else if ((type >= 0x15) && (type <= 0x7F))
     {
       str="Reserved";
     }
     else
     {
       str = "User private";
     }
     break;
  }

return str;
}


//=============================================================================
//  CldemuxStream::getDescriptorString()
//=============================================================================
const char * CldemuxStream::getDescriptorString(int tag)
{
   const char *str;

   switch (tag)
   {
   case 0: str="Reserved"; break;
   case 1: str="Reserved"; break;
   case 2: str="video_stream_descriptor"; break;
   case 3: str="audio_stream_descriptor"; break;
   case 4: str="hierarchy_descriptor"; break;
   case 5: str="registration_descriptor"; break;
   case 6: str="data_stream_alignment_descriptor"; break;
   case 7: str="target_background_grid_descriptor"; break;
   case 8: str="Video_window_descriptor"; break;
   case 9: str="CA_descriptor"; break;
   case 10: str="ISO_639_language_descriptor"; break;
   case 11: str="System_clock_descriptor"; break;
   case 12: str="Multiplex_buffer_utilization_descriptor"; break;
   case 13: str="Copyright_descriptor"; break;
   case 14: str="Maximum_bitrate_descriptor"; break;
   case 15: str="Private_data_indicator_descriptor"; break;
   case 16: str="Smoothing_buffer_descriptor"; break;
   case 17: str="STD_descriptor"; break;
   case 18: str="IBP_descriptor"; break;
   case 19: str="ISO/IEC 13818-6"; break;
   case 20: str="ISO/IEC 13818-6"; break;
   case 21: str="ISO/IEC 13818-6"; break;
   case 22: str="ISO/IEC 13818-6"; break;
   case 23: str="ISO/IEC 13818-6"; break;
   case 24: str="ISO/IEC 13818-6"; break;
   case 25: str="ISO/IEC 13818-6"; break;
   case 26: str="ISO/IEC 13818-6"; break;
   case 27: str="MPEG-4_video_descriptor"; break;
   case 28: str="MPEG-4_audio_descriptor"; break;
   case 29: str="IOD_descriptor"; break;
   case 30: str="SL_descriptor"; break;
   case 31: str="FMC_descriptor"; break;
   case 32: str="External_ES_ID_descriptor"; break;
   case 33: str="MuxCode_descriptor"; break;
   case 34: str="FmxBufferSize_descriptor"; break;
   case 35: str="MultiplexBuffer_descriptor"; break;

   default:
     if ((tag >= 36) && (tag <= 63))
     {
       str = "Reserved";
     }
     else
     {
       str = "User Private";
     }
     break;
   }

   return str;
}


//=============================================================================
//  CldemuxStream::parseStreamInfo()
//=============================================================================
void CldemuxStream::parseStreamInfo(int length)
{
  int bytesRead = 0;
  int count = MAX_STREAMS;

  numStreams = 0;

  while (bytesRead < length && count--)
  {
    bytesRead += parseStream();

    //g_print("parseStreamInfo  length=%d  bytesRead=%d\n", length, bytesRead);
  }
}


//=============================================================================
//  CldemuxStream::parseStream()
//=============================================================================
int CldemuxStream::parseStream()
{
  int bytesRead = 0;
  unsigned char type;
  unsigned char byte;
  unsigned short pid;
  unsigned short esLength;
  int i;

  type     = getSectionByte();
  pid      = getSectionShort();
  esLength = getSectionShort();
  bytesRead = 5;

  pid &= 0x1FFF;
  esLength &= 0xFFF;

  streams[numStreams].type = type;
  streams[numStreams].pid = pid;
  streams[numStreams].esLength = esLength;

  // TODO parse descriptors
  // for now, skip over descriptors
  unsigned char tag;
  unsigned char length;
  unsigned char data;

  // Parse first descriptor for this stream if present
  if (esLength > 0)
  {
    tag = getSectionByte();
    bytesRead++;

    length = getSectionByte();
    bytesRead++;

    streams[numStreams].descriptor.tag = tag;
    streams[numStreams].descriptor.length = length;

    for (i=0; i<length; i++)
    {
      data = getSectionByte();
      streams[numStreams].descriptor.data[i] = data;
    }
    bytesRead += length;

    // The remainder that was not parsed for the first descriptor is skipped
    for (i=0; i<esLength - 2 - length; i++)
    {
      getSectionByte();
      bytesRead++;
    }
  }

  if (numStreams < MAX_STREAMS-1)
  {
    numStreams++;
  }

  return bytesRead;
}


//=============================================================================
//  CldemuxStream::parseDescriptors()
//=============================================================================
void CldemuxStream::parseDescriptors(int length)
{
  int bytesRead = 0;
  int count = MAX_DESCRIPTORS;

  numDescriptors = 0;

  // length is the program_info_length and is the number of bytes of descriptors
  // return number of bytes read
  while (bytesRead < length && count--)
  {
    bytesRead += parseDescriptor();
  }
}


//=============================================================================
//  CldemuxStream::parseDescriptor()
//=============================================================================
int CldemuxStream::parseDescriptor()
{
  int bytesRead = 0;
  unsigned char tag;
  unsigned char length;
  unsigned char data;
  int i;

  tag = getSectionByte();
  bytesRead++;

  length = getSectionByte();
  bytesRead++;

  descriptors[numDescriptors].tag = tag;
  descriptors[numDescriptors].length = length;

  for (i=0; i<length; i++)
  {
    data = getSectionByte();
    descriptors[numDescriptors].data[i] = data;
  }
  bytesRead += length;

  if (numDescriptors < MAX_DESCRIPTORS-1)
  {
    numDescriptors++;
  }

  return bytesRead;
}


//=============================================================================
//  CldemuxStream::parseEISS()
//=============================================================================
void CldemuxStream::parseEISS()
{
  int i;
  unsigned char protocolVersionMajor;
  unsigned char protocolVersionMinor;
  unsigned short appType;
  unsigned char appId[6];
  unsigned char platformIdLength;
  int descriptorLengthTotal;

  // EISS app info descriptor
  unsigned char descriptorTag;
  unsigned char descriptorLength;
  unsigned char appControlCode;
  unsigned char appVersionMajor;
  unsigned char appVersionMinor;
  unsigned char maxProtocolVersionMajor;
  unsigned char maxProtocolVersionMinor;
  unsigned char testFlag;
  unsigned char applicationPriority;

  unsigned short locatorType;
  unsigned short locatorLength;
  unsigned char locatorString[MAX_LOCATOR_STRING];
  unsigned short esLength;
  int bytesRead = 0;


  //if (debugPrintCount >= 10) return;

  //debugPrintCount++;

  bytesRead = parseSectionHeader(0);

  if (tableId == 0xE0)
  {
    g_print("--- EISS table E0 pid=%d\n", pid);
  }
  else if (tableId == 0xE2)
  {
    g_print("--- EISS table E2 pid=%d\n", pid);
  }
  else
  {
    return;
  }


  protocolVersionMajor = getSectionByte();
  protocolVersionMinor = getSectionByte();
  appType = getSectionShort();
  bytesRead += 4;

  // Read app id
  for (i=0; i<6; i++)
  {
    appId[i] = getSectionByte();
    bytesRead++;
  }

  platformIdLength = getSectionByte();
  bytesRead++;

  // Read and discard platform ids
  for (i=0; i<platformIdLength; i++)
  {
    getSectionByte();
    bytesRead++;
  }

  // At this point only the descriptors and the 4 byte CRC should be left
  descriptorLengthTotal = sectionLength - bytesRead - 4;

  // Read the first descriptor
  descriptorTag = getSectionByte();
  bytesRead++;
  descriptorLength = getSectionByte();
  bytesRead++;

  g_print("   major=%d minor=%d appType=%d appId=%.2x %.2x %.2x %.2x %.2x %.2x  platformIdLength=%d descLength=%d\n",
      protocolVersionMajor, protocolVersionMinor, appType,
      appId[0], appId[1], appId[2], appId[3], appId[4], appId[5],
      platformIdLength, descriptorLengthTotal);

  g_print("   descriptor:  tag=0x%X length=%d\n", descriptorTag, descriptorLength);

  if (descriptorTag == 0xE0)
  {
    appControlCode          = getSectionByte();
    appVersionMajor         = getSectionByte();
    appVersionMinor         = getSectionByte();
    maxProtocolVersionMajor = getSectionByte();
    maxProtocolVersionMinor = getSectionByte();
    testFlag                = getSectionByte();
                              getSectionByte();
                              getSectionByte();
                              getSectionByte();
    applicationPriority     = getSectionByte();
    bytesRead += 10;


    // Get locator see EBIF I06 11.14
    locatorType = getSectionShort();
    bytesRead += 2;

    locatorLength = locatorType;
    locatorLength &= 0x03FF;
    locatorType >>= 10;

    g_print("    appControlCode=%d[%s] app ver major=%d minor=%d  protocol ver major=%d minor=%d test=%d prioriy=%d\n",
        appControlCode, getAppCcString(appControlCode), appVersionMajor, appVersionMinor,
        protocolVersionMajor, protocolVersionMinor, testFlag, applicationPriority);

    g_print("    locatorType=%d[%s] locatorLength=%d\n", locatorType, getProtocolString(locatorType), locatorLength);

    if (locatorLength > sectionLength - bytesRead - 4)
    {
      locatorLength = sectionLength - bytesRead - 4;
    }

    esLength = getSectionShort();
    bytesRead += 2;

    for (i=0; i < esLength; i++)
    {
      locatorString[i] = getSectionByte();
      bytesRead++;
    }
    locatorString[i] = 0;

    if (locatorLength > 0)
    {
      g_print("    esLength=%d initial_resource_locator: %s\n", esLength, locatorString);
    }
  }

  g_print("\n");
}


//=============================================================================
//    CldemuxStream::getProtocolString()
//=============================================================================
const char * CldemuxStream::getProtocolString(int type)
{
  const char *str;

  switch (type)
  {
  case 0: str="EBI Heap Locator";               break;
  case 1: str="EBI Table Locator";              break;
  case 2: str="DSM-CC U-N Data Carousel Module";  break;
  case 3: str="DSM-CC U-U Object Carousel Object Locator";    break;
  case 4: str="URI Locator";                    break;
  case 5: str="Mpeg Service Locator";           break;
  case 6: str="Mpeg Component Locator";         break;
  case 7: str="ETV Stream Event Locator";       break;
  case 8: str="Indirect URI Locator";           break;
  case 9: str="Application Locator";            break;
  case 10: str="Async Message Event Locator";   break;

  default:
    str="Reserved";
    break;
  }

  return str;
}


//=============================================================================
//    CldemuxStream::getAppCcString()
//=============================================================================
const char * CldemuxStream::getAppCcString(int type)
{
  const char *str;

  switch (type)
  {
  case 0: str = "reserved";     break;
  case 1: str = "AUTOSTART";    break;
  case 2: str = "PRESENT";      break;
  case 3: str = "DESTROY";      break;

  case 4:
  case 5:
  case 6:
  case 7:
    str = "SUSPEND";
    break;

  default:
    str = "reserved";
    break;
  }

  return str;
}


//=============================================================================
//    CldemuxList::~CldemuxList()
//=============================================================================
CldemuxList::~CldemuxList()
{
  CldemuxListItem *item, *next;

  // Delete all items in this list:
  item = GetFirst();

  while (item)
  {
    next = item->GetNext();
    delete item;
    item = next;
  }

  first = last = NULL;
}


//=============================================================================
//    CldemuxList::AddLast()
//=============================================================================
CldemuxListItem * CldemuxList::AddLast(CldemuxListItem *item)
{
  if (item == NULL)
  {
    return NULL;
  }

  if (last)
  {
    last->SetNext(item);
  }

  item->SetPrevious(last);
  item->SetNext(NULL);

  last = item;

  if (first == NULL)
  {
    first = item;
  }

  return item;
}


//=============================================================================
//    CldemuxList::DebugPrint()
//=============================================================================
void CldemuxList::DebugPrint(unsigned char indent)
{
  // Print debug info about all items in the list:
  CldemuxListItem *item;

  for (item = GetFirst(); item; item = item->GetNext())
  {
    item->DebugPrint(indent);
  }
}


//=============================================================================
//    CldemuxList::AddFirst()
//=============================================================================
CldemuxListItem * CldemuxList::AddFirst(CldemuxListItem *item)
{
  if (item == NULL)
    return NULL;

  item->SetNext(first);
  if (first)
  {
    first->SetPrevious(item);
  }

  item->SetPrevious(NULL);
  first = item;

  if (last == NULL)
  {
    last = item;
  }

  return item;
}


//=============================================================================
//    CldemuxList::Remove()
//=============================================================================
bool CldemuxList::Remove(CldemuxListItem *toBeRemoved)
{
  CldemuxListItem *item;
  CldemuxListItem *next;
  CldemuxListItem *previous;

  if (toBeRemoved == NULL)
  {
     return false;
  }

  // Find the specified item, remove it from the list:
  // Note: this does not delete the removed item.
  for (item = first; item; item = item->GetNext())
  {
     if (item == toBeRemoved)
     {
      next = item->GetNext();
      previous = item->GetPrevious();

      if (next)
      {
        next->SetPrevious(previous);
      }

      if (previous)
      {
        previous->SetNext(next);
      }

      if (item == first)
      {
        first = next;
      }

      if (item == last)
      {
        last = previous;
      }

      return true;
     }
  }

  return false;
}











