#include "stdafx.h"
#include "RM_Manager.h"
#include "str.h"


RC OpenScan(RM_FileScan *rmFileScan,RM_FileHandle *fileHandle,int conNum,Con *conditions)//初始化扫描
{
	return SUCCESS;
}

RC GetNextRec(RM_FileScan *rmFileScan,RM_Record *rec)
{
	return SUCCESS;
}

RC GetRec (RM_FileHandle *fileHandle,RID *rid, RM_Record *rec) 
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	RM_Record *pRec = rec;
	char *pData;	//数据区所在的地址
	char *offset;	//记录所在的地址
	PageNum pageNum = rid->pageNum;
	SlotNum slotNum = rid->slotNum;

	GetThisPage(fileHandle->pfFileHandle, pageNum, pageHandle);
	GetData(pageHandle, &pData);
	offset = pData + fileHandle->fileSubHeader->firstRecordOffset + slotNum * fileHandle->fileSubHeader->recordSize;
	memcpy(pRec, offset, fileHandle->fileSubHeader->recordSize);

	rec = pRec;
	UnpinPage(pageHandle);
	free(pageHandle);

	return SUCCESS;
}

RC InsertRec (RM_FileHandle *fileHandle,char *pData, RID *rid)
{
	return SUCCESS;
}

RC DeleteRec (RM_FileHandle *fileHandle,const RID *rid)
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	char *pData;

	//判断rid是否有效
	if (!rid->bValid) {
		return RM_INVALIDRID;
	}
	GetThisPage(fileHandle->pfFileHandle, rid->pageNum, pageHandle);
	GetData(pageHandle, &pData);

	//修改数据页的bitMap
	char c = ~(1 << (rid->slotNum % 8)); //设置好需要且操作的字节
	//找出bitmap需要修改的字节地址,将对应的bit设置为无效0
	*(pData + rid->slotNum / 8) &= c; 

	//在记录文件控制页中将该页标记为非满页
	c =  ~(1 << (rid->pageNum % 8));
	char *rBitMap = fileHandle->rBitMap;
	*(rBitMap + rid->pageNum / 8) &= c;

	//记录文件控制页记录个数减一
	fileHandle->fileSubHeader->nRecords--;
	
	//判断当前页是否为空页，即全是空槽
	int emptyNum = 0;
	for (; emptyNum < fileHandle->fileSubHeader->recordsPerPage; emptyNum++) {
		if(*(pData + emptyNum / 8) & (1 << (emptyNum % 8))) {
			break;
		}
	}

	if (emptyNum >= fileHandle->fileSubHeader->recordsPerPage) { //为空页
		//删除此页
		DisposePage(fileHandle->pfFileHandle, pageHandle->pFrame->page.pageNum);
	}
	//因为修改了对应数据页和第一页（记录控制页）
	MarkDirty(pageHandle);
	MarkDirty(fileHandle->pageHandle);

	UnpinPage(pageHandle);
	free(pageHandle);

	return SUCCESS;
}

RC UpdateRec (RM_FileHandle *fileHandle,const RM_Record *rec)
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	char *pData;
	//判断记录ID是否有效
	if (!rec->rid.bValid) {
		return RM_INVALIDRID;
	}
	GetThisPage(fileHandle->pfFileHandle, rec->rid.pageNum, pageHandle);
	GetData(pageHandle, &pData);
	/**
	要被替换的记录的地址
	pData + fileHandle->fileSubHeader->firstRecordOffset + fileHandle->fileSubHeader->recordSize * rec->rid.slotNum;
	替换的记录地址
	rec->pData;
	**/
	//替换
	memcpy(pData + fileHandle->fileSubHeader->firstRecordOffset + fileHandle->fileSubHeader->recordSize * rec->rid.slotNum,
		rec->pData, fileHandle->fileSubHeader->recordSize);
	
	MarkDirty(pageHandle);
	UnpinPage(pageHandle);
	free(pageHandle);

	return SUCCESS;
}

RC RM_CreateFile (char *fileName, int recordSize)
{

	PF_FileHandle *fileHandle = (PF_FileHandle *)malloc(sizeof(PF_FileHandle));
	PF_PageHandle *pageHandle0 = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	PF_PageHandle *pageHandle1 = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	RM_FileSubHeader *rmFileSubHeader = (RM_FileSubHeader *)malloc(sizeof(RM_FileSubHeader));

	char *pData;

	int recordsPerPage, numBitMapByte;//bitmap占用字节数
	
	//创建页式文件
	CreateFile(fileName);
	//打开
	openFile(fileName, fileHandle);
	//获取第0页页面句柄
	GetThisPage(fileHandle, 0, pageHandle0);
	MarkDirty(pageHandle0);

	//分配新页面作为记录控制页面,即页面1
	AllocatePage(fileHandle, pageHandle1);
	//记录控制页，第一页的处理
	GetData(pageHandle1, &pData); //获取记录控制页的数据区地址
	rmFileSubHeader->nRecords = 0;
	rmFileSubHeader->recordSize = recordSize;
	recordsPerPage = (4092 * 8) / (1 + 8 * recordSize);
	numBitMapByte = recordsPerPage / 8;
	if (recordsPerPage % 8) {
		numBitMapByte++;
	}
	rmFileSubHeader->firstRecordOffset = numBitMapByte;
	rmFileSubHeader->recordsPerPage = recordsPerPage;
	memcpy(pData, (char *)rmFileSubHeader, RM_FILESUBHDR_SIZE); //复制
	MarkDirty(pageHandle1);

	//解除驻留缓冲区限制
	UnpinPage(pageHandle0);
	UnpinPage(pageHandle1);

	//结束的处理
	CloseFile(fileHandle);
	free(rmFileSubHeader);

	return SUCCESS;
}

RC RM_OpenFile(char *fileName, RM_FileHandle *fileHandle)
{
	RM_FileHandle *rmFileHandle = fileHandle;
	PF_FileHandle *pfFileHandle = (PF_FileHandle *)malloc(sizeof(PF_FileHandle));
	PF_PageHandle *pfPageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	char *pData;
	
	//打开文件
	if(openFile(fileName, pfFileHandle) == PF_FILEERR) {
		return PF_FILEERR;
	}
	//页面管理
	rmFileHandle->bOpen = true;
	rmFileHandle->pfFileHandle = pfFileHandle;
	
	//记录文件管理页面，第一页
	GetThisPage(pfFileHandle, 1, pfPageHandle);
	GetData(pfPageHandle, &pData);
	rmFileHandle->rBitMap = pData + RM_FILESUBHDR_SIZE;
	rmFileHandle->fileSubHeader = (RM_FileSubHeader *)pData;
	rmFileHandle->pageHandle = pfPageHandle;
	
	fileHandle = rmFileHandle;

	return SUCCESS;
}

RC RM_CloseFile(RM_FileHandle *fileHandle)
{
	UnpinPage(fileHandle->pageHandle);
	return CloseFile(fileHandle->pfFileHandle);
}
