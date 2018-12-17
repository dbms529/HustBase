#include "stdafx.h"
#include "RM_Manager.h"
#include "str.h"


RC CloseScan(RM_FileScan *rmFileScan) 
{
	rmFileScan->bOpen = false;
	rmFileScan->conditions = NULL;
	rmFileScan->conNum = 0;
	rmFileScan->pRMFileHandle = NULL;
	rmFileScan->pn = 0;
	rmFileScan->sn = 0;
	if (rmFileScan->pageHandle != NULL) {
		UnpinPage(rmFileScan->pageHandle);
		free(rmFileScan->pageHandle);
		rmFileScan->pageHandle = NULL;
	}
	return SUCCESS;
}

RC OpenScan(RM_FileScan *rmFileScan, RM_FileHandle *fileHandle, int conNum, Con *conditions)//��ʼ��ɨ��
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	RC rc;
	char *pBitMap = fileHandle->pfFileHandle->pBitmap;
	char *pData;
	rmFileScan->bOpen = true;
	rmFileScan->conditions = conditions;
	rmFileScan->conNum = conNum;
	rmFileScan->pRMFileHandle = fileHandle;
	
	/*����pageHandle, pn, sn*/
	if (fileHandle->pfFileHandle->pFileSubHeader->nAllocatedPages <= 2) { //û���ѷ���ҳ
		rmFileScan->pageHandle = NULL;
		rmFileScan->pn = 0;
		rmFileScan->sn = 0;
	} else {
		//�ҵ���һ���ѷ���ҳ�ĵ�һ����Ч��¼
		for (unsigned int i = 2; i <= fileHandle->pfFileHandle->pFileSubHeader->pageCount; i++) {
			if (((*(pBitMap + i / 8)) & (1 << (i % 8))) != 0) { //��ǰҳ���ѷ���ҳ
				//�ҵ���һ����ռ�õļ�¼��
				if ((rc = GetThisPage(fileHandle->pfFileHandle, i, pageHandle)) != SUCCESS) { //���ҳ����
					return rc;
				}
				if ((rc = GetData(pageHandle, &pData)) != SUCCESS) { 
					return rc;
				}
				for (int j = 0; j < fileHandle->fileSubHeader->recordsPerPage; j++) {
					if (((*(pData + j / 8)) & (1 << j % 8)) != 0) { //�ҵ���Ч���
						rmFileScan->pageHandle = pageHandle;
						rmFileScan->pn = i;
						rmFileScan->sn = j;
						break;
					}
				}
				break;
			}
		}
	}
	
	return SUCCESS;
}

RC GetNextRec(RM_FileScan *rmFileScan, RM_Record *rec)
{
	if (rmFileScan->pn == 0) {
		return RM_EOF;
	}

	RID *rid = (RID *)malloc(sizeof(RID));
	RM_Record *pRec = rec;
	RC rc;
	Con *con = rmFileScan->conditions; //������ʼ��ַ
	int conNum = rmFileScan->conNum; //��������
	char *pfBitMap = rmFileScan->pRMFileHandle->pfFileHandle->pBitmap;
	PF_PageHandle *pageHandle = rmFileScan->pageHandle; //pageHandle
	char *pData;

	rid->bValid = true;
	rid->pageNum = rmFileScan->pn; //ҳ��
	rid->slotNum = rmFileScan->sn; //��ۺ�
	
	if (rmFileScan->conNum == 0) { //����Ϊ��
		if ((rc = GetRec(rmFileScan->pRMFileHandle, rid, pRec)) != SUCCESS) { //ֱ�ӻ�ȡ��¼
			return rc;
		}
		//�����µ�pn, pageHandle, sn
		unsigned int tmpPn = rid->pageNum;
		int tmpSn = rid->slotNum + 1;
		rid->bValid = false;

		for (; tmpPn <= rmFileScan->pRMFileHandle->pfFileHandle->pFileSubHeader->pageCount; tmpPn++) { //����ҳ
			if (((*(pfBitMap + tmpPn / 8)) & (1 << tmpPn % 8)) != 0) { //����Чҳ
				if ((rc = GetThisPage(rmFileScan->pRMFileHandle->pfFileHandle, tmpPn, pageHandle)) != SUCCESS) {
					return rc;
				}
				if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
					return rc;
				}

				for (; tmpSn < rmFileScan->pRMFileHandle->fileSubHeader->recordsPerPage; tmpSn++) { //������ǰҳ�Ĳ�
					if (((*(pData + tmpSn / 8)) & (1 << tmpSn % 8)) != 0) { //����Ч��
						rid->bValid = true;
						rid->pageNum = tmpPn;
						rid->slotNum = tmpSn;
						break;
					}
				}

				if (rid->bValid) { //�ҵ���һ����¼��
					break;
				} else { //��ǰҳû����һ����¼
					UnpinPage(pageHandle);
				}
			}
		}

		if (!rid->bValid) { //û����һ����¼��
			rid->pageNum = 0;
			rid->slotNum = 0;
		}

	} else { //������Ϊ��
		char *pData;
		int bLhsIsAttr, bRhsIsAttr;
		CompOp compOp;
		int LattrLength, LattrOffset, RattrLength, RattrOffset;
		void *Lvalue, *Rvalue;
		
		while (true) { //��ѭ�����ҳ�����������һ����¼, ���ҳ���һ����¼��pn, sn��pageHandle
			if ((rc = GetRec(rmFileScan->pRMFileHandle, rid, pRec)) != SUCCESS) { //ֱ�ӻ�ȡ��¼
				return rc;
			}
			pData = pRec->pData; //��¼��������ʼ��ַ
			
			//�ж��ҳ��ļ�¼�Ƿ�������������
			int i = 0; //�����������
			bool flag; //�Ƿ��������
			for (; i < conNum; i++) {
				flag = false; 
				/*һЩ��ֵ*/
				bLhsIsAttr = (con + i)->bLhsIsAttr;
				bRhsIsAttr = (con + i)->bRhsIsAttr;
				compOp = (con + i)->compOp;
				AttrType attrType = (con + i)->attrType;
				LattrLength = (con + i)->LattrLength;
				LattrOffset = (con + i)->LattrOffset;
				RattrLength = (con + i)->RattrLength;
				RattrOffset = (con + i)->RattrOffset;
				Lvalue = (con + i)->Lvalue;
				Rvalue = (con + i)->Rvalue;

				switch (attrType) {
				case chars:
					char *charLv, *charRv;
					if (bLhsIsAttr) {
						strncpy(charLv, pData + LattrOffset, LattrLength);
					} else {
						charLv = (char *)Lvalue;
					}
					if (bRhsIsAttr) {
						strncpy(charRv, pData + RattrOffset, RattrLength);
					} else {
						charRv = (char *)Rvalue;
					}

					flag = charCompare(compOp, charLv, charRv);
					break;
				case ints:
					int intLv, intRv;
					if (bLhsIsAttr) {
						intLv = *(int *)(pData + LattrOffset);
					} else {
						intLv = *(int *)Lvalue;
					}
					if (bRhsIsAttr) {
						intRv = *(int *)(pData + RattrOffset);
					} else {
						intRv = *(int *)Rvalue;
					}
					
					flag = intCompare(compOp, intLv, intRv);
					break;
				case floats:
					float floatLv, floatRv;
					if (bLhsIsAttr) {
						floatLv = *(float *)(pData + LattrOffset);
					} else {
						floatLv = *(float *)Lvalue;
					}
					if (bRhsIsAttr) {
						floatRv = *(float *)(pData + RattrOffset);
					} else {
						floatRv = *(float *)Rvalue;
					}

					flag = floatCompare(compOp, floatLv, floatRv);
					break;
				default:
					break;
				}

				if (!flag) { //����������
					break;
				}
			}

			//�����µ�pn, pageHandle, sn
			unsigned int tmpPn = rid->pageNum;
			int tmpSn = rid->slotNum + 1;
			rid->bValid = false;

			for (; tmpPn <= rmFileScan->pRMFileHandle->pfFileHandle->pFileSubHeader->pageCount; tmpPn++) { //����ҳ
				if (((*(pfBitMap + tmpPn / 8)) & (1 << tmpPn % 8)) != 0) { //����Чҳ
					if ((rc = GetThisPage(rmFileScan->pRMFileHandle->pfFileHandle, tmpPn, pageHandle)) != SUCCESS) {
						return rc;
					}
					if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
						return rc;
					}
					
					for (; tmpSn < rmFileScan->pRMFileHandle->fileSubHeader->recordsPerPage; tmpSn++) { //������ǰҳ�Ĳ�
						if (((*(pData + tmpSn / 8)) & (1 << tmpSn % 8)) != 0) { //����Ч��
							rid->bValid = true;
							rid->pageNum = tmpPn;
							rid->slotNum = tmpSn;
							break;
						}
					}

					if (rid->bValid) { //�ҵ���һ����¼��
						break;
					} else { //��ǰҳû��������¼
						UnpinPage(pageHandle);
					}
				}
			}

			if (!rid->bValid) { //û�ҵ���һ����¼��˵���������
				rid->pageNum = 0;
				rid->slotNum = 0;
				break;
			}

			if (i >= conNum) {	//����ǰ��¼������������
				break; //����whileѭ��
			}

		}
		
	} 

	/*�޸�rmFileScan*/
	rmFileScan->pageHandle = pageHandle;
	rmFileScan->pn = rid->pageNum;
	rmFileScan->sn = rid->slotNum;
	
	rec = pRec;
	free(rid);

	return SUCCESS;
}

bool charCompare(CompOp op, char *charLv, char *charRv) {
	bool flag = false;
	int result = strcmp(charLv, charLv);
	switch (op) {
	case EQual:	// =
		if (result == 0) {
			flag = true;
		}
		break;
	case LEqual: // <=
		if (result <= 0) {
			flag = true;
		}
		break;
	case NEqual: // !=
		if (result != 0) {
			flag = true;
		}
		break;
	case LessT: // <
		if (result < 0) {
			flag = true;
		}
		break;
	case GEqual: // >=
		if (result >= 0) {
			flag = true;
		}
		break;
	case GreatT: // >
		if (result > 0) {
			flag = true;
		}
		break;
	default: // NO_OP
		break;
	}
	return flag;
}

bool intCompare(CompOp op, int intLv, int intRv) {
	bool flag = false;
	switch (op) {
	case EQual: // =
		if (intLv == intRv) {
			flag = true;
		}
		break;
	case LEqual: // <=
		if (intLv <= intRv) {
			flag = true;
		}
		break;
	case NEqual: // !=
		if (intLv <= intRv) {
			flag = true;
		}
		break;
	case LessT: // <
		if (intLv < intRv) {
			flag = true;
		}
		break;
	case GEqual: // >=
		if (intLv >= intRv) {
			flag = true;
		}
		break;
	case GreatT: // >
		if (intLv > intRv) {
			flag = true;
		}
		break;
	default: // NO_OP
		break;
	}

	return flag;
}

bool floatCompare(CompOp op, float floatLv, float floatRv) {
	bool flag = false;
	switch (op) {
	case EQual: // =
		if (floatLv == floatRv) {
			flag = true;
		}
		break;
	case LEqual: // <=
		if (floatLv <= floatRv) {
			flag = true;
		}
		break;
	case NEqual: // !=
		if (floatLv != floatRv) {
			flag = true;
		}
		break;
	case LessT: // <
		if (floatLv < floatRv) {
			flag = true;
		}
		break;
	case GEqual: // >=
		if (floatLv >= floatRv) {
			flag = true;
		}
		break;
	case GreatT: // >
		if (floatLv > floatRv) {
			flag = true;
		}
		break;
	default:
		break;
	}

	return flag;
}

RC GetRec (RM_FileHandle *fileHandle, RID *rid, RM_Record *rec) 
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	RM_Record *pRec = rec;
	char *pData;	//���������ڵĵ�ַ
	char *offset;	//��¼���ڵĵ�ַ
	RC rc;
	PageNum pageNum = rid->pageNum;
	SlotNum slotNum = rid->slotNum;

	if ((rc = GetThisPage(fileHandle->pfFileHandle, pageNum, pageHandle)) != SUCCESS) {
		return rc;
	}
	if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
		return rc;
	}
	
	offset = pData + fileHandle->fileSubHeader->firstRecordOffset + slotNum * fileHandle->fileSubHeader->recordSize;
	memcpy(pRec, offset, fileHandle->fileSubHeader->recordSize);

	rec = pRec;
	UnpinPage(pageHandle);
	free(pageHandle);

	return SUCCESS;
}

RC InsertRec (RM_FileHandle *fileHandle, char *pData, RID *rid)
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	RID *pRid = rid;
	RC rc;
	int endPageNum = fileHandle->pfFileHandle->pFileSubHeader->pageCount; //��ȡβҳ��ҳ��
	int allocatePageNum = fileHandle->pfFileHandle->pFileSubHeader->nAllocatedPages; //�ѷ���ҳ����Ŀ
	char *pfBitMap = fileHandle->pfFileHandle->pBitmap;
	char *rmBitMap = fileHandle->rBitMap;
	char *pDataAddr; //data��ַ

	//�ҵ�һ������ҳ
	for (int i = 2; i <= endPageNum; i++) {
		if (((*(pfBitMap + i / 8)) & (1 << (i % 8))) != 0) { //��ǰҳ���ѷ���ҳ
			if (((*(rmBitMap + i / 8)) & (1 << (i % 8))) != 0) { //�����ҳ������
				continue;
			} else { //��ҳ��δ��
				//�Ҳ۲����¼ 
				if ((rc = GetThisPage(fileHandle->pfFileHandle, i, pageHandle)) != SUCCESS) {
					return rc;
				}
				if ((rc = GetData(pageHandle, &pDataAddr)) != SUCCESS) {
					return rc;
				}
				for (int j = 0; j < fileHandle->fileSubHeader->recordsPerPage; j++) {
					if (((*(pDataAddr + j / 8)) & (1 << (j % 8))) == 0) { //�ҵ����
						pRid->bValid = true;
						pRid->pageNum = i;
						pRid->slotNum = j;
						break;
					}
				}
				if (pRid->bValid) {
					break;
				}
			}
		}
	}

	//���Ƿ��ҵ���ۣ�δ�ҵ��������ҳ�棬���޸�pRid
	if (!pRid->bValid) {//û���ҵ�
		//������ҳ��
		if ((rc = AllocatePage(fileHandle->pfFileHandle, pageHandle)) != SUCCESS) {
			return rc;
		}

		//�޸�pRid
		pRid->bValid = true;
		if ((rc = GetPageNum(pageHandle, &(pRid->pageNum))) != SUCCESS) {
			return rc;
		}
		pRid->slotNum = 0;

		//��pDataAddr��Ϊ��ǰ����ҳ����������ַ
		if ((rc = GetData(pageHandle, &pDataAddr)) != SUCCESS) {
			return rc;
		}
		//����¼����ҳbitmap��Ӧҳ����Ϊ0
		*(rmBitMap + pRid->pageNum / 8) &= ~(1 << pRid->pageNum % 8);
	}

	//�ڲ������Ӽ�¼
	memcpy(pDataAddr + fileHandle->fileSubHeader->firstRecordOffset + pRid->slotNum * fileHandle->fileSubHeader->recordSize,
			pData, fileHandle->fileSubHeader->recordSize);

	/**�޸�bitmap(s)���Լ���������ҳ**/
	*(pDataAddr + pRid->slotNum / 8) |= 1 << (pRid->slotNum % 8); //�޸Ĳ����¼������ҳ��bitmap
	fileHandle->fileSubHeader->nRecords++; //�޸ļ�¼����ҳ��fileSubHeader
	*(pfBitMap + pRid->pageNum / 8) |= 1<< (pRid->pageNum % 8); //�޸�ҳ�����ҳ��bitmap

	/**�޸ļ�¼����ҳ��bitmap**/ 
	//�жϲ����¼������ҳ�Ƿ�����
	int posNum = pRid->slotNum + 1; //��ռ�õĲ�۸���
	for (; posNum < fileHandle->fileSubHeader->recordsPerPage; posNum++) {
		if (((*(pDataAddr + posNum / 8)) & (1 << posNum % 8)) == 0) { //��һ���ղ۾��˳�
			break;
		}
	}
	if (posNum >= fileHandle->fileSubHeader->recordsPerPage) { //��ǰҳ����
		//�޸ļ�¼����ҳ��bitmap
		*(rmBitMap + rid->pageNum / 8) |= (1 << rid->pageNum % 8);
	}

	MarkDirty(pageHandle);
	MarkDirty(fileHandle->pageHandle);
	UnpinPage(pageHandle);
	free(pageHandle);

	rid = pRid;

	return SUCCESS;
}

RC DeleteRec (RM_FileHandle *fileHandle, const RID *rid)
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	char *pData;
	RC rc;

	//�ж�rid�Ƿ���Ч
	if (!rid->bValid) {
		return RM_INVALIDRID;
	}

	if ((rc = GetThisPage(fileHandle->pfFileHandle, rid->pageNum, pageHandle)) != SUCCESS) {
		return rc;
	}
	if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
		return rc;
	}
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

RC UpdateRec (RM_FileHandle *fileHandle, const RM_Record *rec)
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	char *pData;
	RC rc;

	//�жϼ�¼ID�Ƿ���Ч
	if (!rec->rid.bValid) {
		return RM_INVALIDRID;
	}
	if ((rc = GetThisPage(fileHandle->pfFileHandle, rec->rid.pageNum, pageHandle)) != SUCCESS) {
		return rc;
	}
	if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
		return rc;
	}
	/**
	Ҫ���滻�ļ�¼�ĵ�ַ
	pData + fileHandle->fileSubHeader->firstRecordOffset + fileHandle->fileSubHeader->recordSize * rec->rid.slotNum;
	�滻�ļ�¼�ĵ�ַ
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
	RC rc;
	char *pData;

	int recordsPerPage, numBitMapByte;//bitmapռ���ֽ���
	
	//����ҳʽ�ļ�
	if ((rc =  CreateFile(fileName)) != SUCCESS) {
		return rc;
	}
	//��
	if ((rc = openFile(fileName, fileHandle)) != SUCCESS) {
		return rc;
	}
	//��ȡ��0ҳҳ����
	if ((rc = GetThisPage(fileHandle, 0, pageHandle0)) != SUCCESS) {
		return rc;
	}
	MarkDirty(pageHandle0);

	//������ҳ����Ϊ��¼����ҳ��,��ҳ��1
	AllocatePage(fileHandle, pageHandle1);
	//��¼����ҳ����һҳ�Ĵ���
	if ((rc = GetData(pageHandle1, &pData)) != SUCCESS) { //��ȡ��¼����ҳ����������ַ
		return rc;
	}
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
	//bitmap���ô���
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
	RC rc;

	//���ļ�
	if ((rc = openFile(fileName, pfFileHandle)) != SUCCESS) {
		return rc;
	}
	//ҳ�����
	rmFileHandle->bOpen = true;
	rmFileHandle->pfFileHandle = pfFileHandle;
	
	//��¼�ļ�����ҳ�棬��һҳ
	if ((rc = GetThisPage(pfFileHandle, 1, pfPageHandle)) != SUCCESS) {
		return rc;
	}
	if ((rc = GetData(pfPageHandle, &pData)) != SUCCESS) {
		return rc;
	}
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
