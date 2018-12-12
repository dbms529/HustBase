#include "stdafx.h"
#include "RM_Manager.h"
#include "str.h"


RC OpenScan(RM_FileScan *rmFileScan,RM_FileHandle *fileHandle,int conNum,Con *conditions)//��ʼ��ɨ��
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
	char *pData;	//���������ڵĵ�ַ
	char *offset;	//��¼���ڵĵ�ַ
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

	//�ж�rid�Ƿ���Ч
	if (!rid->bValid) {
		return RM_INVALIDRID;
	}
	GetThisPage(fileHandle->pfFileHandle, rid->pageNum, pageHandle);
	GetData(pageHandle, &pData);

	//�޸�����ҳ��bitMap
	char c = ~(1 << (rid->slotNum % 8)); //���ú���Ҫ�Ҳ������ֽ�
	//�ҳ�bitmap��Ҫ�޸ĵ��ֽڵ�ַ,����Ӧ��bit����Ϊ��Ч0
	*(pData + rid->slotNum / 8) &= c; 

	//�ڼ�¼�ļ�����ҳ�н���ҳ���Ϊ����ҳ
	c =  ~(1 << (rid->pageNum % 8));
	char *rBitMap = fileHandle->rBitMap;
	*(rBitMap + rid->pageNum / 8) &= c;

	//��¼�ļ�����ҳ��¼������һ
	fileHandle->fileSubHeader->nRecords--;
	
	//�жϵ�ǰҳ�Ƿ�Ϊ��ҳ����ȫ�ǿղ�
	int emptyNum = 0;
	for (; emptyNum < fileHandle->fileSubHeader->recordsPerPage; emptyNum++) {
		if(*(pData + emptyNum / 8) & (1 << (emptyNum % 8))) {
			break;
		}
	}

	if (emptyNum >= fileHandle->fileSubHeader->recordsPerPage) { //Ϊ��ҳ
		//ɾ����ҳ
		DisposePage(fileHandle->pfFileHandle, pageHandle->pFrame->page.pageNum);
	}
	//��Ϊ�޸��˶�Ӧ����ҳ�͵�һҳ����¼����ҳ��
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
	//�жϼ�¼ID�Ƿ���Ч
	if (!rec->rid.bValid) {
		return RM_INVALIDRID;
	}
	GetThisPage(fileHandle->pfFileHandle, rec->rid.pageNum, pageHandle);
	GetData(pageHandle, &pData);
	/**
	Ҫ���滻�ļ�¼�ĵ�ַ
	pData + fileHandle->fileSubHeader->firstRecordOffset + fileHandle->fileSubHeader->recordSize * rec->rid.slotNum;
	�滻�ļ�¼��ַ
	rec->pData;
	**/
	//�滻
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

	int recordsPerPage, numBitMapByte;//bitmapռ���ֽ���
	
	//����ҳʽ�ļ�
	CreateFile(fileName);
	//��
	openFile(fileName, fileHandle);
	//��ȡ��0ҳҳ����
	GetThisPage(fileHandle, 0, pageHandle0);
	MarkDirty(pageHandle0);

	//������ҳ����Ϊ��¼����ҳ��,��ҳ��1
	AllocatePage(fileHandle, pageHandle1);
	//��¼����ҳ����һҳ�Ĵ���
	GetData(pageHandle1, &pData); //��ȡ��¼����ҳ����������ַ
	rmFileSubHeader->nRecords = 0;
	rmFileSubHeader->recordSize = recordSize;
	recordsPerPage = (4092 * 8) / (1 + 8 * recordSize);
	numBitMapByte = recordsPerPage / 8;
	if (recordsPerPage % 8) {
		numBitMapByte++;
	}
	rmFileSubHeader->firstRecordOffset = numBitMapByte;
	rmFileSubHeader->recordsPerPage = recordsPerPage;
	memcpy(pData, (char *)rmFileSubHeader, RM_FILESUBHDR_SIZE); //����
	MarkDirty(pageHandle1);

	//���פ������������
	UnpinPage(pageHandle0);
	UnpinPage(pageHandle1);

	//�����Ĵ���
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
	
	//���ļ�
	if(openFile(fileName, pfFileHandle) == PF_FILEERR) {
		return PF_FILEERR;
	}
	//ҳ�����
	rmFileHandle->bOpen = true;
	rmFileHandle->pfFileHandle = pfFileHandle;
	
	//��¼�ļ�����ҳ�棬��һҳ
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
