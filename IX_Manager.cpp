#include "stdafx.h"
#include "IX_Manager.h"

// �����ݺ�RID����ɹؼ���
char * dataPack(void *pData, const RID *rid, int attrLength);

// �Թؼ���(����+RID)���бȽ�
const int keycmp(char *keyA, char *keyB, int attrLength);

void InsertKey(char *keys, RID *rids, char *key, int keyNum, int attrLength, int keyLength);

// ͨ�������ļ������ҳ��Ż�ȡ�ڵ���Ϣ
const RC GetIndexNode(IX_IndexHandle *indexHandle, PageNum pageNum, IX_Node *node);

char * dataPack(void *pData, const RID *rid, int attrLength)
{
	char *key = (char *)malloc(sizeof(RID) + attrLength);
	memcpy(key, pData, attrLength);
	memcpy(&(key[attrLength]), rid, sizeof(RID));
	return key;
}

const int keycmp(char *keyA, char *keyB, int attrLength)
{
	int cmpRc = strncmp(keyA, keyB, attrLength);
	if (cmpRc != 0)
	{
		return cmpRc;
	}
	else
	{
		RID *ridA = (RID *)(&keyA[attrLength]), *ridB = (RID *)(&keyB[attrLength]);
		if (ridA->pageNum > ridB->pageNum)
		{
			return 1;
		}
		else if (ridA->pageNum < ridB->pageNum)
		{
			return -1;
		}
		else
		{
			if (ridA->slotNum > ridB->slotNum)
			{
				return 1;
			}
			else if (ridA->slotNum < ridB->slotNum)
			{
				return -1;
			}
			else
			{
				return 0;
			}
		}
	}
}

const RC GetIndexNode(IX_IndexHandle *indexHandle, PageNum pageNum, IX_Node *node)
{
	RC rc;
	char *data;
	IX_Node *pnode;
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));

	rc = GetThisPage(&(indexHandle->fileHandle), pageNum, pageHandle);
	if (rc != SUCCESS)
	{
		return rc;
	}

	rc = GetData(pageHandle, &data);
	if (rc != SUCCESS)
	{
		return rc;
	}

	pnode = (IX_Node *)(data + sizeof(IX_FileHeader));
	*node = *pnode;

	return SUCCESS;
}

void InsertKey(char *keys, RID *rids, char *key, int keyNum, int attrLength, int keyLength)
{
	int i, j;
	for (i = 0; i < keyNum; i++)
	{
		if (keycmp(&(keys[keyLength * i]), key, attrLength) < 0)
		{
			break;
		}
	}
	for (j = keyNum - 1; j > i; j--)
	{
		memcpy(&(keys[(j + 1) * keyLength]), key, keyLength);
		memcpy(&(rids[(j + 1)]), );

	}


}

RC CreateIndex(const char * fileName,AttrType attrType,int attrLength)
{
	RC rc;
	PF_FileHandle *fileHandle = (PF_FileHandle *)malloc(sizeof(PF_FileHandle));
	PF_PageHandle *first_pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	IX_FileHeader *indexFileHeader;
	char *data;
	// ������ͨҳ���ļ�
	rc = CreateFile(fileName);
	if (rc != SUCCESS)
	{
		return rc;
	}

	rc = openFile((char *)fileName, fileHandle);
	if (rc != SUCCESS)
	{
		return rc;
	}
	// �����һ��ҳ��
	rc = AllocatePage(fileHandle, first_pageHandle);
	if (rc != SUCCESS)
	{
		return rc;
	}
	// ���һ��ҳ����д�������Ϣ
	rc = GetData(first_pageHandle, &data);
	if (rc != SUCCESS)
	{
		return rc;
	}
	indexFileHeader = (IX_FileHeader *)data;
	indexFileHeader->attrLength = attrLength;
	indexFileHeader->attrType = attrType;
	indexFileHeader->first_leaf = 0; // 0 ��ʾ��ǰ���޽ڵ�
	indexFileHeader->keyLength = attrLength + sizeof(RID);
	indexFileHeader->order = (PF_PAGESIZE - sizeof(IX_FileHeader) - sizeof(IX_Node)) / (2 * sizeof(RID) + attrLength);
	indexFileHeader->rootPage = 0;
	
	CloseFile(fileHandle);
	free(fileHandle);
	free(first_pageHandle);
	return SUCCESS;
}

RC OpenIndex(const char *fileName,IX_IndexHandle *indexHandle)
{
	RC rc;
	char *data;
	PF_PageHandle *first_pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	IX_FileHeader *indexFileHeader;
	// ���ļ�
	rc = openFile((char *)fileName, &(indexHandle->fileHandle));
	if (rc != SUCCESS)
	{
		return rc;
	}
	// ��ȡ��һҳ�洢�������ļ�ͷ
	rc = GetThisPage(&(indexHandle->fileHandle), 1, first_pageHandle);
	if (rc != SUCCESS)
	{
		return rc;
	}

	rc = GetData(first_pageHandle, &data);
	if (rc != SUCCESS)
	{
		return rc;
	}

	indexFileHeader = (IX_FileHeader *)data;
	indexHandle->fileHeader = *indexFileHeader;

	return SUCCESS;
}

RC CloseIndex(IX_IndexHandle *indexHandle)
{
	// ���ùر�ҳ���ļ�
	return (CloseFile(&(indexHandle->fileHandle)));
}

RC InsertEntry(IX_IndexHandle *indexHandle,void *pData,const RID * rid)
{
	RC rc;
	PageNum rootPageNum;
	IX_Node *pNode, *newNode;
	int i, flag;
	int order = indexHandle->fileHeader.order;
	int attrLength = indexHandle->fileHeader.attrLength;
	int keyLength = indexHandle->fileHeader.keyLength;
	int cmpRc;
	char *key = dataPack(pData, rid, attrLength);

	// ��ȡ���ڵ������ļ�
	rootPageNum = indexHandle->fileHeader.rootPage;
	if (rootPageNum == 0)
	{
		// ��ǰ��Ϊ��:�����µĸ��ڵ�
		PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
		IX_FileHeader *indexFileHeader;
		char *data;

		rc = GetThisPage(&(indexHandle->fileHandle), 1, pageHandle);
		if (rc != SUCCESS)
		{
			return rc;
		}

		rc = GetData(pageHandle, &data);
		if (rc != SUCCESS)
		{
			return rc;
		}

		pNode = (IX_Node *)(data + sizeof(IX_FileHeader));
		pNode->brother = 0;
		pNode->is_leaf = 1;
		pNode->keynum = 1;
		pNode->keys = data + sizeof(IX_FileHeader) + sizeof(IX_Node);
		pNode->rids = (RID *)(pNode->keys + indexHandle->fileHeader.order * (sizeof(RID) + indexHandle->fileHeader.attrLength));
		pNode->parent = 0;
		
		indexHandle->fileHeader.first_leaf = 1;
		indexHandle->fileHeader.rootPage = 1;

		// ͬʱ���������ļ�ͷ
		indexFileHeader = (IX_FileHeader *)data;
		indexFileHeader->first_leaf = 1;
		indexFileHeader->rootPage = 1;
		free(pageHandle);
	}
	else
	{
		// ���ҵ����ڵ�
		PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
		char *data;

		rc = GetThisPage(&(indexHandle->fileHandle), rootPageNum, pageHandle);
		if (rc != SUCCESS)
		{
			return rc;
		}

		rc = GetData(pageHandle, &data);
		if (rc != SUCCESS)
		{
			return rc;
		}

		// �ҵ�Ҷ�ӽڵ�
		pNode = (IX_Node *)(data + sizeof(IX_FileHeader));
		pNode->keys = data + sizeof(IX_FileHeader) + sizeof(IX_Node);
		pNode->rids = (RID *)(pNode->keys + indexHandle->fileHeader.order * (sizeof(RID) + indexHandle->fileHeader.attrLength));
		while(!(pNode->is_leaf))
		{
			flag = 0;
			for (i = 0; i < pNode->keynum; i++)
			{
				//if ()
			}

		}
		if (pNode->keynum < order)
		{

		}


	}

	
	
	

}

RC DeleteEntry(IX_IndexHandle *indexHandle,void *pData,const RID * rid)
{

}

RC OpenIndexScan(IX_IndexScan *indexScan,IX_IndexHandle *indexHandle,CompOp compOp,char *value){
	return SUCCESS;
}

RC IX_GetNextEntry(IX_IndexScan *indexScan,RID * rid){
	return SUCCESS;
}

RC CloseIndexScan(IX_IndexScan *indexScan){
	return SUCCESS;
}

// ******
RC GetIndexTree(char *fileName, Tree *index){
	RC rc;
	PageNum rootPage;
	IX_IndexHandle *indexHandle = (IX_IndexHandle *)malloc(sizeof(IX_IndexHandle));
	IX_Node *rootNode = (IX_Node *)malloc(sizeof(IX_Node));

	rc = OpenIndex(fileName, indexHandle);
	if (rc != SUCCESS)
	{
		return rc;
	}

	rootPage = indexHandle->fileHeader.rootPage;
	index->attrLength = indexHandle->fileHeader.attrLength;
	index->attrType = indexHandle->fileHeader.attrType;
	index->order = indexHandle->fileHeader.order;
	if (rootPage == 0)
	{
		index->root = NULL;
	}
	else
	{
		/*rc = GetIndexNode(indexHandle, rootPage, &(index->root));
		if (rc != SUCCESS)
		{
			return rc;
		}*/
	}
	return SUCCESS;
}


