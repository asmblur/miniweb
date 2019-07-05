/////////////////////////////////////////////////////////////////////////////
//
// httphandler.c
//
// MiniWeb - mini webserver implementation
//
/////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include "httppil.h"
#include "httpapi.h"
#include "revision.h"
#ifdef _7Z
#include "7zDec/7zInc.h"
#endif
#ifdef _ZIP
#include "minz/miniz.h"
#endif
#include "httpxml.h"

int _mwBuildHttpHeader(HttpSocket *phsSocket, time_t contentDateTime, unsigned char* buffer);
void _mwCloseSocket(HttpParam* hp, HttpSocket* phsSocket);

//////////////////////////////////////////////////////////////////////////
// callback from the web server whenever a valid request comes in
//////////////////////////////////////////////////////////////////////////
int uhStats(UrlHandlerParam* param)
{
	char *p;
	char buf[384];
	HttpStats *stats=&((HttpParam*)param->hp)->stats;
	HttpRequest *req=&param->hs->request;
	IPADDR ip = param->hs->ipAddr;
	HTTP_XML_NODE node;
	int bufsize = param->dataBytes;
	int ret=FLAG_DATA_RAW;

	mwGetHttpDateTime(time(NULL), buf, sizeof(buf));

	if (stats->clientCount>4) {
		param->pucBuffer=(char*)malloc(stats->clientCount*256+1024);
		ret=FLAG_DATA_RAW | FLAG_TO_FREE;
	}

	p=param->pucBuffer;

	//generate XML
	mwWriteXmlHeader(&p, &bufsize, 10, 0, 0);

	mwWriteXmlString(&p, &bufsize, 0, "<ServerStats>");

	sprintf(buf, "%d.%d.%d.%d", ip.caddr[3], ip.caddr[2], ip.caddr[1], ip.caddr[0]);

	node.indent = 1;
	node.name = "ClientIP";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%s", buf);

	node.name = "UpTime";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%ld", (long)(time(NULL) - stats->startTime));

	node.name = "Clients";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%d", (int)stats->clientCount);

	node.name = "ExpireTimeout";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%d", (int)stats->timeOutCount);

	node.name = "MaxClients";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%d", (int)stats->clientCountMax);

	node.name = "Requests";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%u", (unsigned int)stats->reqCount);

	node.name = "FileSent";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%u", (unsigned int)stats->fileSentCount);

	node.name = "FileSentMB";
	mwWriteXmlLine(&p, &bufsize, &node, 0, "%u", (unsigned int)(stats->fileSentBytes >> 20));

	mwWriteXmlString(&p, &bufsize, 1, "<Clients>");

	{
		HttpSocket *phsSocketCur;
		time_t curtime=time(NULL);
		int i;
		for (i = 0; i < ((HttpParam*)param->hp)->maxClients; i++) {
			phsSocketCur = ((HttpParam*)param->hp)->hsSocketQueue + i;
			if (!phsSocketCur->socket) continue;
			ip=phsSocketCur->ipAddr;
			sprintf(buf,"<Client ip=\"%d.%d.%d.%d\" requests=\"%d\" expire=\"%d\" speed=\"%u\" path=\"%s\"/>",
				ip.caddr[3],ip.caddr[2],ip.caddr[1],ip.caddr[0], phsSocketCur->iRequestCount, (int)(phsSocketCur->tmExpirationTime - curtime),
				(unsigned int)(phsSocketCur->response.sentBytes / (((curtime - phsSocketCur->tmAcceptTime) << 10) + 1)), phsSocketCur->request.pucPath);
			mwWriteXmlString(&p, &bufsize, 2, buf);
			/*
			if (phsSocketCur->request.pucPath)
				p+=sprintf(p,"(%d/%d)",phsSocketCur->response.iSentBytes,phsSocketCur->response.iContentLength);
			else
				p+=sprintf(p,"(idle)");
			*/
		}
	}

	mwWriteXmlString(&p, &bufsize, 1, "</Clients>");
	mwWriteXmlString(&p, &bufsize, 0, "</ServerStats>");

	//return data to server
	param->dataBytes=(int)(p - param->pucBuffer);
	param->fileType=MIMETYPE_XML;
	return ret;
}

#ifdef _7Z

int uh7Zip(UrlHandlerParam* param)
{
	HttpRequest *req=&param->hs->request;
	HttpParam *hp= (HttpParam*)param->hp;
	char *path;
	char *filename;
	void *content;
	int len;
	char *p = strchr(req->pucPath, '/');
	if (p) p = strchr(p + 1, '/');
	if (!p) return 0;
	filename = p + 1;
	*p = 0;
	path = (char*)malloc(strlen(req->pucPath) + strlen(hp->pchWebPath) + 5);
	sprintf(path, "%s/%s.7z", hp->pchWebPath, req->pucPath);
	printf("7zRequest pucpath: %s\n webPath: %s \npath: %s\n", req->pucPath, hp->pchWebPath, path);
	*p = '/';

	if (!IsFileExist(path)) {
		free(path);
		return 0;
	}

	len = SzExtractContent(hp->szctx, path, filename, &content);
	free(path);
	if (len < 0) return 0;

	p = strrchr(filename, '.');
  param->fileType = Mime_getTypeFromExt(p);
	param->dataBytes = len;
	param->pucBuffer = content;
	return FLAG_DATA_RAW;
}

#endif

#ifdef _ZIP

int uhZip(UrlHandlerParam* param)
{
  HttpRequest *req = &param->hs->request;
  HttpParam *hp = (HttpParam*)param->hp;
  char *path;
  char *filename;
  void *content;
  int len = -1;
  char *p = strchr(req->pucPath, '/');
  if (p) p = strchr(p + 1, '/');
  if (!p) return 0;
  filename = p + 1;
  *p = 0;
  path = (char*)malloc(strlen(req->pucPath) + strlen(hp->pchWebPath) + 5);
  sprintf(path, "%s/%s.zip", hp->pchWebPath, req->pucPath);
  printf("zip Request pucpath: %s\n webPath: %s \npath: %s\n", req->pucPath, hp->pchWebPath, path);
  *p = '/';

  if (!IsFileExist(path)) {
    free(path);
    return 0;
  }

  content = mz_zip_extract_archive_file_to_heap(path, filename, &len, 0);
  free(path);
  if (len < 0 || content == NULL) return 0;

  p = strrchr(filename, '.');
  param->fileType = Mime_getTypeFromExt(p);
  param->dataBytes = len;
  param->pucBuffer = content;
  return FLAG_DATA_RAW;
}

#endif


//////////////////////////////////////////////////////////////////////////
// stream handler sample
//////////////////////////////////////////////////////////////////////////
#ifdef HAVE_THREAD
typedef struct {
	int state;
	pthread_t thread;
	char result[16];
} HANDLER_DATA;

void* WriteContent(HANDLER_DATA* hdata)
{
	char *p = hdata->result;
	int i;
	for (i = 0; i < 10; i++, p++) {
		*p = '0' + i;
		msleep(100);
	}
	*p = 0;
	return 0;
}

int uhAsyncDataTest(UrlHandlerParam* param)
{
	int ret = FLAG_DATA_STREAM | FLAG_TO_FREE;
	HANDLER_DATA* hdata = (HANDLER_DATA*)param->hs->ptr;

	if (param->pucBuffer) {
		if (!hdata) {
			// first invoke
			hdata = param->hs->ptr = calloc(1, sizeof(HANDLER_DATA));
			ThreadCreate(&hdata->thread, WriteContent, hdata);
			param->dataBytes = 0;
		} else {
			if (hdata->state == 1) {
				// done
				ret = 0;
			} else if (ThreadWait(hdata->thread, 10, 0)) {
				// data not ready
				param->dataBytes = 0;
			} else {
				// data ready
				strcpy(param->pucBuffer, hdata->result);
				param->dataBytes = strlen(param->pucBuffer);
				hdata->state = 1;
			}
		}
	} else {
		// cleanup
		ret = 0;
	}
	param->fileType=HTTPFILETYPE_TEXT;
	return ret;
}
#endif
