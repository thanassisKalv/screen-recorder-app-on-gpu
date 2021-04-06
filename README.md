# screen-recorder-cpu
this version of screen recording is a Windows desktop application too. However is much less efficient that the gpu recorder.<br/> 

The application implements the producer-consumer pattern using a thread-safe Queue implementation to manage the stream of frames<br/> 

The ScreenGrabber class is based on the API of [Wingdi](https://docs.microsoft.com/en-us/windows/win32/gdi/windows-gdi) . The producer thread uses it to geneate and push new bitmap screenshot frames in the Queue.<br/> 
The VideoEncoder class utilizes the [MMF Sink Writer](https://docs.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer) to encode and save the stream of frames.

## System requirements
 - project has been saved with VS2017
 - ensure that the [required codecs](https://www.codecguide.com/media_foundation_codecs.htm) exist in local system
