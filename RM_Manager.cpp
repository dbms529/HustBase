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

RC OpenScan(RM_FileScan *rmFileScan, RM_FileHandle *fileHandle, int conNum, Con *conditions)//初始化扫描
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	RC rc;
	char *pBitMap = fileHandle->pfFileHandle->pBitmap;
	char *pData;
	rmFileScan->bOpen = true;
	rmFileScan->conditions = conditions;
	rmFileScan->conNum = conNum;
	rmFileScan->pRMFileHandle = fileHandle;
	
	/*处理pageHandle, pn, sn*/
	if (fileHandle->pfFileHandle->pFileSubHeader->nAllocatedPages <= 2) { //没有已分配页
		rmFileScan->pageHandle = NULL;
		rmFileScan->pn = 0;
		rmFileScan->sn = 0;
	} else {
		//找到第一个已分配页的第一条有效记录
		for (unsigned int i = 2; i <= fileHandle->pfFileHandle->pFileSubHeader->pageCount; i++) {
			if (((*(pBitMap + i / 8)) & (1 << (i % 8))) != 0) { //当前页是已分配页
				//找到第一个被占用的记录槽
				if ((rc = GetThisPage(fileHandle->pfFileHandle, i, pageHandle)) != SUCCESS) { //获得页面句柄
					return rc;
				}
				if ((rc = GetData(pageHandle, &pData)) != SUCCESS) { 
					return rc;
				}
				for (int j = 0; j < fileHandle->fileSubHeader->recordsPerPage; j++) {
					if (((*(pData + j / 8)) & (1 << j % 8)) != 0) { //找到有效插槽
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
	Con *con = rmFileScan->conditions; //条件起始地址
	int conNum = rmFileScan->conNum; //条件数量
	char *pfBitMap = rmFileScan->pRMFileHandle->pfFileHandle->pBitmap;
	PF_PageHandle *pageHandle = rmFileScan->pageHandle; //pageHandle
	char *pData;

	rid->bValid = true;
	rid->pageNum = rmFileScan->pn; //页号
	rid->slotNum = rmFileScan->sn; //插槽号
	
	if (rmFileScan->conNum == 0) { //条件为空
		if ((rc = GetRec(rmFileScan->pRMFileHandle, rid, pRec)) != SUCCESS) { //直接获取记录
			return rc;
		}
		//设置新的pn, pageHandle, sn
		unsigned int tmpPn = rid->pageNum;
		int tmpSn = rid->slotNum + 1;
		rid->bValid = false;

		for (; tmpPn <= rmFileScan->pRMFileHandle->pfFileHandle->pFileSubHeader->pageCount; tmpPn++) { //遍历页
			if (((*(pfBitMap + tmpPn / 8)) & (1 << tmpPn % 8)) != 0) { //是有效页
				if ((rc = GetThisPage(rmFileScan->pRMFileHandle->pfFileHandle, tmpPn, pageHandle)) != SUCCESS) {
					return rc;
				}
				if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
					return rc;
				}

				for (; tmpSn < rmFileScan->pRMFileHandle->fileSubHeader->recordsPerPage; tmpSn++) { //遍历当前页的槽
					if (((*(pData + tmpSn / 8)) & (1 << tmpSn % 8)) != 0) { //是有效槽
						rid->bValid = true;
						rid->pageNum = tmpPn;
						rid->slotNum = tmpSn;
						break;
					}
				}

				if (rid->bValid) { //找到下一个纪录了
					break;
				} else { //当前页没有下一条记录
					UnpinPage(pageHandle);
				}
			}
		}

		if (!rid->bValid) { //没有下一条记录了
			rid->pageNum = 0;
			rid->slotNum = 0;
		}

	} else { //条件不为空
		char *pData;
		int bLhsIsAttr, bRhsIsAttr;
		CompOp compOp;
		int LattrLength, LattrOffset, RattrLength, RattrOffset;
		void *Lvalue, *Rvalue;
		
		while (true) { //此循环来找出满足条件的一条记录, 并找出下一条记录的pn, sn，pageHandle
			if ((rc = GetRec(rmFileScan->pRMFileHandle, rid, pRec)) != SUCCESS) { //直接获取记录
				return rc;
			}
			pData = pRec->pData; //记录的数据起始地址
			
			//判断找出的记录是否满足所有条件
			int i = 0; //满足的条件数
			bool flag; //是否符合条件
			for (; i < conNum; i++) {
				flag = false; 
				/*一些赋值*/
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

				if (!flag) { //不符合条件
					break;
				}
			}

			//设置新的pn, pageHandle, sn
			unsigned int tmpPn = rid->pageNum;
			int tmpSn = rid->slotNum + 1;
			rid->bValid = false;

			for (; tmpPn <= rmFileScan->pRMFileHandle->pfFileHandle->pFileSubHeader->pageCount; tmpPn++) { //遍历页
				if (((*(pfBitMap + tmpPn / 8)) & (1 << tmpPn % 8)) != 0) { //是有效页
					if ((rc = GetThisPage(rmFileScan->pRMFileHandle->pfFileHandle, tmpPn, pageHandle)) != SUCCESS) {
						return rc;
					}
					if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
						return rc;
					}
					
					for (; tmpSn < rmFileScan->pRMFileHandle->fileSubHeader->recordsPerPage; tmpSn++) { //遍历当前页的槽
						if (((*(pData + tmpSn / 8)) & (1 << tmpSn % 8)) != 0) { //是有效槽
							rid->bValid = true;
							rid->pageNum = tmpPn;
							rid->slotNum = tmpSn;
							break;
						}
					}

					if (rid->bValid) { //找到下一个纪录了
						break;
					} else { //当前页没有下条记录
						UnpinPage(pageHandle);
					}
				}
			}

			if (!rid->bValid) { //没找到下一条记录，说明遍历完成
				rid->pageNum = 0;
				rid->slotNum = 0;
				break;
			}

			if (i >= conNum) {	//当当前记录满足所有条件
				break; //跳出while循环
			}

		}
		
	} 

	/*修改rmFileScan*/
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
	char *pData;	//数据区所在的地址
	char *offset;	//记录所在的地址
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
	int endPageNum = fileHandle->pfFileHandle->pFileSubHeader->pageCount; //获取尾页的页号
	int allocatePageNum = fileHandle->pfFileHandle->pFileSubHeader->nAllocatedPages; //已分配页的数目
	char *pfBitMap = fileHandle->pfFileHandle->pBitmap;
	char *rmBitMap = fileHandle->rBitMap;
	char *pDataAddr; //data地址

	//找到一个非满页
	for (int i = 2; i <= endPageNum; i++) {
		if (((*(pfBitMap + i / 8)) & (1 << (i % 8))) != 0) { //当前页是已分配页
			if (((*(rmBitMap + i / 8)) & (1 << (i % 8))) != 0) { //如果该页面已满
				continue;
			} else { //该页面未满
				//找槽插入记录 
				if ((rc = GetThisPage(fileHandle->pfFileHandle, i, pageHandle)) != SUCCESS) {
					return rc;
				}
				if ((rc = GetData(pageHandle, &pDataAddr)) != SUCCESS) {
					return rc;
				}
				for (int j = 0; j < fileHandle->fileSubHeader->recordsPerPage; j++) {
					if (((*(pDataAddr + j / 8)) & (1 << (j % 8))) == 0) { //找到插槽
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

	//看是否找到插槽，未找到则分配新页面，并修改pRid
	if (!pRid->bValid) {//没有找到
		//分配新页面
		if ((rc = AllocatePage(fileHandle->pfFileHandle, pageHandle)) != SUCCESS) {
			return rc;
		}

		//修改pRid
		pRid->bValid = true;
		if ((rc = GetPageNum(pageHandle, &(pRid->pageNum))) != SUCCESS) {
			return rc;
		}
		pRid->slotNum = 0;

		//将pDataAddr改为当前数据页的数据区地址
		if ((rc = GetData(pageHandle, &pDataAddr)) != SUCCESS) {
			return rc;
		}
		//将记录控制页bitmap对应页设置为0
		*(rmBitMap + pRid->pageNum / 8) &= ~(1 << pRid->pageNum % 8);
	}

	//在插槽中添加记录
	memcpy(pDataAddr + fileHandle->fileSubHeader->firstRecordOffset + pRid->slotNum * fileHandle->fileSubHeader->recordSize,
			pData, fileHandle->fileSubHeader->recordSize);

	/**修改bitmap(s)，以及两个控制页**/
	*(pDataAddr + pRid->slotNum / 8) |= 1 << (pRid->slotNum % 8); //修改插入记录的数据页的bitmap
	fileHandle->fileSubHeader->nRecords++; //修改记录控制页的fileSubHeader
	*(pfBitMap + pRid->pageNum / 8) |= 1<< (pRid->pageNum % 8); //修改页面控制页的bitmap

	/**修改记录控制页的bitmap**/ 
	//判断插入记录的数据页是否已满
	int posNum = pRid->slotNum + 1; //被占用的插槽个数
	for (; posNum < fileHandle->fileSubHeader->recordsPerPage; posNum++) {
		if (((*(pDataAddr + posNum / 8)) & (1 << posNum % 8)) == 0) { //有一个空槽就退出
			break;
		}
	}
	if (posNum >= fileHandle->fileSubHeader->recordsPerPage) { //当前页已满
		//修改记录控制页的bitmap
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

	//判断rid是否有效
	if (!rid->bValid) {
		return RM_INVALIDRID;
	}

	if ((rc = GetThisPage(fileHandle->pfFileHandle, rid->pageNum, pageHandle)) != SUCCESS) {
		return rc;
	}
	if ((rc = GetData(pageHandle, &pData)) != SUCCESS) {
		return rc;
	}
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

RC UpdateRec (RM_FileHandle *fileHandle, const RM_Record *rec)
{
	PF_PageHandle *pageHandle = (PF_PageHandle *)malloc(sizeof(PF_PageHandle));
	char *pData;
	RC rc;

	//判断记录ID是否有效
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
	要被替换的记录的地址
	pData + fileHandle->fileSubHeader->firstRecordOffset + fileHandle->fileSubHeader->recordSize * rec->rid.slotNum;
	替换的记录的地址
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
	RC rc;
	char *pData;

	int recordsPerPage, numBitMapByte;//bitmap占用字节数
	
	//创建页式文件
	if ((rc =  CreateFile(fileName)) != SUCCESS) {
		return rc;
	}
	//打开
	if ((rc = openFile(fileName, fileHandle)) != SUCCESS) {
		return rc;
	}
	//获取第0页页面句柄
	if ((rc = GetThisPage(fileHandle, 0, pageHandle0)) != SUCCESS) {
		return rc;
	}
	MarkDirty(pageHandle0);

	//分配新页面作为记录控制页面,即页面1
	AllocatePage(fileHandle, pageHandle1);
	//记录控制页，第一页的处理
	if ((rc = GetData(pageHandle1, &pData)) != SUCCESS) { //获取记录控制页的数据区地址
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
	memcpy(pData, (char *)rmFileSubHeader, RM_FILESUBHDR_SIZE); //复制
	//bitmap不用处理
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
	RC rc;

	//打开文件
	if ((rc = openFile(fileName, pfFileHandle)) != SUCCESS) {
		return rc;
	}
	//页面管理
	rmFileHandle->bOpen = true;
	rmFileHandle->pfFileHandle = pfFileHandle;
	
	//记录文件管理页面，第一页
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
