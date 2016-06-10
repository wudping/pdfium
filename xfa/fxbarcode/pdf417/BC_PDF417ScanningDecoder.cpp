// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com
// Original code is licensed as follows:
/*
 * Copyright 2013 ZXing authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>

#include "xfa/fxbarcode/BC_DecoderResult.h"
#include "xfa/fxbarcode/BC_ResultPoint.h"
#include "xfa/fxbarcode/common/BC_CommonBitMatrix.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417BarcodeMetadata.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417BarcodeValue.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417BoundingBox.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417Codeword.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417CodewordDecoder.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417Common.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417DecodedBitStreamParser.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417DecodedBitStreamParser.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417DecodedBitStreamParser.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417DetectionResult.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417DetectionResultColumn.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417DetectionResultRowIndicatorColumn.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417ECErrorCorrection.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417ECModulusGF.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417ECModulusPoly.h"
#include "xfa/fxbarcode/pdf417/BC_PDF417ScanningDecoder.h"
#include "xfa/fxbarcode/utils.h"

int32_t CBC_PDF417ScanningDecoder::CODEWORD_SKEW_SIZE = 2;
int32_t CBC_PDF417ScanningDecoder::MAX_ERRORS = 3;
int32_t CBC_PDF417ScanningDecoder::MAX_EC_CODEWORDS = 512;
CBC_PDF417ECErrorCorrection* CBC_PDF417ScanningDecoder::errorCorrection =
    nullptr;

CBC_PDF417ScanningDecoder::CBC_PDF417ScanningDecoder() {}
CBC_PDF417ScanningDecoder::~CBC_PDF417ScanningDecoder() {}
void CBC_PDF417ScanningDecoder::Initialize() {
  errorCorrection = new CBC_PDF417ECErrorCorrection;
}
void CBC_PDF417ScanningDecoder::Finalize() {
  delete errorCorrection;
}
CBC_CommonDecoderResult* CBC_PDF417ScanningDecoder::decode(
    CBC_CommonBitMatrix* image,
    CBC_ResultPoint* imageTopLeft,
    CBC_ResultPoint* imageBottomLeft,
    CBC_ResultPoint* imageTopRight,
    CBC_ResultPoint* imageBottomRight,
    int32_t minCodewordWidth,
    int32_t maxCodewordWidth,
    int32_t& e) {
  CBC_BoundingBox* boundingBox = new CBC_BoundingBox(
      image, imageTopLeft, imageBottomLeft, imageTopRight, imageBottomRight, e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  CBC_DetectionResultRowIndicatorColumn* leftRowIndicatorColumn = nullptr;
  CBC_DetectionResultRowIndicatorColumn* rightRowIndicatorColumn = nullptr;
  CBC_DetectionResult* detectionResult = nullptr;
  for (int32_t i = 0; i < 2; i++) {
    if (imageTopLeft) {
      leftRowIndicatorColumn =
          getRowIndicatorColumn(image, boundingBox, *imageTopLeft, TRUE,
                                minCodewordWidth, maxCodewordWidth);
    }
    if (imageTopRight) {
      rightRowIndicatorColumn =
          getRowIndicatorColumn(image, boundingBox, *imageTopRight, FALSE,
                                minCodewordWidth, maxCodewordWidth);
    }
    detectionResult = merge(leftRowIndicatorColumn, rightRowIndicatorColumn, e);
    if (e != BCExceptionNO) {
      e = BCExceptiontNotFoundInstance;
      delete leftRowIndicatorColumn;
      delete rightRowIndicatorColumn;
      delete boundingBox;
      return nullptr;
    }
    if (i == 0 && (detectionResult->getBoundingBox()->getMinY() <
                       boundingBox->getMinY() ||
                   detectionResult->getBoundingBox()->getMaxY() >
                       boundingBox->getMaxY())) {
      delete boundingBox;
      boundingBox = detectionResult->getBoundingBox();
    } else {
      detectionResult->setBoundingBox(boundingBox);
      break;
    }
  }
  int32_t maxBarcodeColumn = detectionResult->getBarcodeColumnCount() + 1;
  detectionResult->setDetectionResultColumn(0, leftRowIndicatorColumn);
  detectionResult->setDetectionResultColumn(maxBarcodeColumn,
                                            rightRowIndicatorColumn);
  FX_BOOL leftToRight = !!leftRowIndicatorColumn;
  for (int32_t barcodeColumnCount = 1; barcodeColumnCount <= maxBarcodeColumn;
       barcodeColumnCount++) {
    int32_t barcodeColumn = leftToRight ? barcodeColumnCount
                                        : maxBarcodeColumn - barcodeColumnCount;
    if (detectionResult->getDetectionResultColumn(barcodeColumn)) {
      continue;
    }
    CBC_DetectionResultColumn* detectionResultColumn = nullptr;
    if (barcodeColumn == 0 || barcodeColumn == maxBarcodeColumn) {
      detectionResultColumn = new CBC_DetectionResultRowIndicatorColumn(
          boundingBox, barcodeColumn == 0);
    } else {
      detectionResultColumn = new CBC_DetectionResultColumn(boundingBox);
    }
    detectionResult->setDetectionResultColumn(barcodeColumn,
                                              detectionResultColumn);
    int32_t startColumn = -1;
    int32_t previousStartColumn = startColumn;
    for (int32_t imageRow = boundingBox->getMinY();
         imageRow <= boundingBox->getMaxY(); imageRow++) {
      startColumn =
          getStartColumn(detectionResult, barcodeColumn, imageRow, leftToRight);
      if (startColumn < 0 || startColumn > boundingBox->getMaxX()) {
        if (previousStartColumn == -1) {
          continue;
        }
        startColumn = previousStartColumn;
      }
      CBC_Codeword* codeword = detectCodeword(
          image, boundingBox->getMinX(), boundingBox->getMaxX(), leftToRight,
          startColumn, imageRow, minCodewordWidth, maxCodewordWidth);
      if (codeword) {
        detectionResultColumn->setCodeword(imageRow, codeword);
        previousStartColumn = startColumn;
        minCodewordWidth = minCodewordWidth < codeword->getWidth()
                               ? minCodewordWidth
                               : codeword->getWidth();
        maxCodewordWidth = maxCodewordWidth > codeword->getWidth()
                               ? maxCodewordWidth
                               : codeword->getWidth();
      }
    }
  }
  CBC_CommonDecoderResult* decoderresult =
      createDecoderResult(detectionResult, e);
  if (e != BCExceptionNO) {
    delete detectionResult;
    return nullptr;
  }
  return decoderresult;
}

CFX_ByteString CBC_PDF417ScanningDecoder::toString(
    CBC_BarcodeValueArrayArray* barcodeMatrix) {
  CFX_ByteString result;
  for (int32_t row = 0; row < barcodeMatrix->GetSize(); row++) {
    result += row;
    for (int32_t l = 0; l < barcodeMatrix->GetAt(row)->GetSize(); l++) {
      CBC_BarcodeValue* barcodeValue = barcodeMatrix->GetAt(row)->GetAt(l);
      if (barcodeValue->getValue()->GetSize() == 0) {
        result += "";
      } else {
        result += barcodeValue->getValue()->GetAt(0);
        result +=
            barcodeValue->getConfidence(barcodeValue->getValue()->GetAt(0));
      }
    }
  }
  return result;
}

CBC_DetectionResult* CBC_PDF417ScanningDecoder::merge(
    CBC_DetectionResultRowIndicatorColumn* leftRowIndicatorColumn,
    CBC_DetectionResultRowIndicatorColumn* rightRowIndicatorColumn,
    int32_t& e) {
  if (!leftRowIndicatorColumn && !rightRowIndicatorColumn) {
    e = BCExceptionIllegalArgument;
    return nullptr;
  }
  CBC_BarcodeMetadata* barcodeMetadata =
      getBarcodeMetadata(leftRowIndicatorColumn, rightRowIndicatorColumn);
  if (!barcodeMetadata) {
    e = BCExceptionCannotMetadata;
    return nullptr;
  }
  CBC_BoundingBox* leftboundingBox =
      adjustBoundingBox(leftRowIndicatorColumn, e);
  if (e != BCExceptionNO) {
    delete barcodeMetadata;
    return nullptr;
  }
  CBC_BoundingBox* rightboundingBox =
      adjustBoundingBox(rightRowIndicatorColumn, e);
  if (e != BCExceptionNO) {
    delete barcodeMetadata;
    return nullptr;
  }
  CBC_BoundingBox* boundingBox =
      CBC_BoundingBox::merge(leftboundingBox, rightboundingBox, e);
  if (e != BCExceptionNO) {
    delete barcodeMetadata;
    return nullptr;
  }
  CBC_DetectionResult* detectionresult =
      new CBC_DetectionResult(barcodeMetadata, boundingBox);
  return detectionresult;
}
CBC_BoundingBox* CBC_PDF417ScanningDecoder::adjustBoundingBox(
    CBC_DetectionResultRowIndicatorColumn* rowIndicatorColumn,
    int32_t& e) {
  if (!rowIndicatorColumn)
    return nullptr;

  CFX_Int32Array* rowHeights = rowIndicatorColumn->getRowHeights(e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  int32_t maxRowHeight = getMax(*rowHeights);
  int32_t missingStartRows = 0;
  for (int32_t i = 0; i < rowHeights->GetSize(); i++) {
    int32_t rowHeight = rowHeights->GetAt(i);
    missingStartRows += maxRowHeight - rowHeight;
    if (rowHeight > 0) {
      break;
    }
  }
  CFX_ArrayTemplate<CBC_Codeword*>* codewords =
      rowIndicatorColumn->getCodewords();
  for (int32_t row = 0; missingStartRows > 0 && !codewords->GetAt(row); row++) {
    missingStartRows--;
  }
  int32_t missingEndRows = 0;
  for (int32_t row1 = rowHeights->GetSize() - 1; row1 >= 0; row1--) {
    missingEndRows += maxRowHeight - rowHeights->GetAt(row1);
    if (rowHeights->GetAt(row1) > 0) {
      break;
    }
  }
  for (int32_t row2 = codewords->GetSize() - 1;
       missingEndRows > 0 && !codewords->GetAt(row2); row2--) {
    missingEndRows--;
  }
  CBC_BoundingBox* boundingBox =
      rowIndicatorColumn->getBoundingBox()->addMissingRows(
          missingStartRows, missingEndRows, rowIndicatorColumn->isLeft(), e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  return boundingBox;
}
int32_t CBC_PDF417ScanningDecoder::getMax(CFX_Int32Array& values) {
  int32_t maxValue = -1;
  for (int32_t i = 0; i < values.GetSize(); i++) {
    int32_t value = values.GetAt(i);
    maxValue = maxValue > value ? maxValue : value;
  }
  return maxValue;
}
CBC_BarcodeMetadata* CBC_PDF417ScanningDecoder::getBarcodeMetadata(
    CBC_DetectionResultRowIndicatorColumn* leftRowIndicatorColumn,
    CBC_DetectionResultRowIndicatorColumn* rightRowIndicatorColumn) {
  CBC_BarcodeMetadata* leftBarcodeMetadata =
      leftRowIndicatorColumn ? leftRowIndicatorColumn->getBarcodeMetadata()
                             : nullptr;
  if (!leftBarcodeMetadata) {
    return rightRowIndicatorColumn
               ? rightRowIndicatorColumn->getBarcodeMetadata()
               : nullptr;
  }

  CBC_BarcodeMetadata* rightBarcodeMetadata =
      rightRowIndicatorColumn ? rightRowIndicatorColumn->getBarcodeMetadata()
                              : nullptr;
  if (!rightBarcodeMetadata) {
    return leftRowIndicatorColumn ? leftRowIndicatorColumn->getBarcodeMetadata()
                                  : nullptr;
  }

  if (leftBarcodeMetadata->getColumnCount() !=
          rightBarcodeMetadata->getColumnCount() &&
      leftBarcodeMetadata->getErrorCorrectionLevel() !=
          rightBarcodeMetadata->getErrorCorrectionLevel() &&
      leftBarcodeMetadata->getRowCount() !=
          rightBarcodeMetadata->getRowCount()) {
    delete leftBarcodeMetadata;
    delete rightBarcodeMetadata;
    return nullptr;
  }
  delete rightBarcodeMetadata;
  return leftBarcodeMetadata;
}
CBC_DetectionResultRowIndicatorColumn*
CBC_PDF417ScanningDecoder::getRowIndicatorColumn(CBC_CommonBitMatrix* image,
                                                 CBC_BoundingBox* boundingBox,
                                                 CBC_ResultPoint startPoint,
                                                 FX_BOOL leftToRight,
                                                 int32_t minCodewordWidth,
                                                 int32_t maxCodewordWidth) {
  CBC_DetectionResultRowIndicatorColumn* rowIndicatorColumn =
      new CBC_DetectionResultRowIndicatorColumn(boundingBox, leftToRight);
  for (int32_t i = 0; i < 2; i++) {
    int32_t increment = i == 0 ? 1 : -1;
    int32_t startColumn = (int32_t)startPoint.GetX();
    for (int32_t imageRow = (int32_t)startPoint.GetY();
         imageRow <= boundingBox->getMaxY() &&
         imageRow >= boundingBox->getMinY();
         imageRow += increment) {
      CBC_Codeword* codeword =
          detectCodeword(image, 0, image->GetWidth(), leftToRight, startColumn,
                         imageRow, minCodewordWidth, maxCodewordWidth);
      if (codeword) {
        rowIndicatorColumn->setCodeword(imageRow, codeword);
        if (leftToRight) {
          startColumn = codeword->getStartX();
        } else {
          startColumn = codeword->getEndX();
        }
      }
    }
  }
  return rowIndicatorColumn;
}

void CBC_PDF417ScanningDecoder::adjustCodewordCount(
    CBC_DetectionResult* detectionResult,
    CBC_BarcodeValueArrayArray* barcodeMatrix,
    int32_t& e) {
  std::unique_ptr<CFX_Int32Array> numberOfCodewords(
      barcodeMatrix->GetAt(0)->GetAt(1)->getValue());
  int32_t calculatedNumberOfCodewords =
      detectionResult->getBarcodeColumnCount() *
          detectionResult->getBarcodeRowCount() -
      getNumberOfECCodeWords(detectionResult->getBarcodeECLevel());
  if (numberOfCodewords->GetSize() == 0) {
    if (calculatedNumberOfCodewords < 1 ||
        calculatedNumberOfCodewords >
            CBC_PDF417Common::MAX_CODEWORDS_IN_BARCODE) {
      e = BCExceptiontNotFoundInstance;
      BC_EXCEPTION_CHECK_ReturnVoid(e);
    }
    barcodeMatrix->GetAt(0)->GetAt(1)->setValue(calculatedNumberOfCodewords);
  } else if (numberOfCodewords->GetAt(0) != calculatedNumberOfCodewords) {
    barcodeMatrix->GetAt(0)->GetAt(1)->setValue(calculatedNumberOfCodewords);
  }
}

CBC_CommonDecoderResult* CBC_PDF417ScanningDecoder::createDecoderResult(
    CBC_DetectionResult* detectionResult,
    int32_t& e) {
  std::unique_ptr<CBC_BarcodeValueArrayArray> barcodeMatrix(
      createBarcodeMatrix(detectionResult));
  adjustCodewordCount(detectionResult, barcodeMatrix.get(), e);
  if (e != BCExceptionNO) {
    for (int32_t i = 0; i < barcodeMatrix->GetSize(); i++) {
      CBC_BarcodeValueArray* temp = barcodeMatrix->GetAt(i);
      for (int32_t j = 0; j < temp->GetSize(); j++)
        delete temp->GetAt(j);
      delete temp;
    }
    return nullptr;
  }
  CFX_Int32Array erasures;
  CFX_Int32Array codewords;
  codewords.SetSize(detectionResult->getBarcodeRowCount() *
                    detectionResult->getBarcodeColumnCount());
  CFX_ArrayTemplate<CFX_Int32Array*> ambiguousIndexValuesList;
  CFX_Int32Array ambiguousIndexesList;
  for (int32_t row = 0; row < detectionResult->getBarcodeRowCount(); row++) {
    for (int32_t l = 0; l < detectionResult->getBarcodeColumnCount(); l++) {
      CFX_Int32Array* values =
          barcodeMatrix->GetAt(row)->GetAt(l + 1)->getValue();
      int32_t codewordIndex =
          row * detectionResult->getBarcodeColumnCount() + l;
      if (values->GetSize() == 0) {
        erasures.Add(codewordIndex);
      } else if (values->GetSize() == 1) {
        codewords[codewordIndex] = values->GetAt(0);
      } else {
        ambiguousIndexesList.Add(codewordIndex);
        ambiguousIndexValuesList.Add(values);
      }
    }
  }
  CFX_ArrayTemplate<CFX_Int32Array*> ambiguousIndexValues;
  ambiguousIndexValues.SetSize(ambiguousIndexValuesList.GetSize());
  for (int32_t i = 0; i < ambiguousIndexValues.GetSize(); i++) {
    ambiguousIndexValues.SetAt(i, ambiguousIndexValuesList.GetAt(i));
  }
  for (int32_t l = 0; l < barcodeMatrix->GetSize(); l++) {
    CBC_BarcodeValueArray* temp = barcodeMatrix->GetAt(l);
    for (int32_t j = 0; j < temp->GetSize(); j++)
      delete temp->GetAt(j);
    temp->RemoveAll();
    delete temp;
  }
  CBC_CommonDecoderResult* decoderResult =
      createDecoderResultFromAmbiguousValues(
          detectionResult->getBarcodeECLevel(), codewords, erasures,
          ambiguousIndexesList, ambiguousIndexValues, e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  return decoderResult;
}

CBC_CommonDecoderResult*
CBC_PDF417ScanningDecoder::createDecoderResultFromAmbiguousValues(
    int32_t ecLevel,
    CFX_Int32Array& codewords,
    CFX_Int32Array& erasureArray,
    CFX_Int32Array& ambiguousIndexes,
    CFX_ArrayTemplate<CFX_Int32Array*>& ambiguousIndexValues,
    int32_t& e) {
  CFX_Int32Array ambiguousIndexCount;
  ambiguousIndexCount.SetSize(ambiguousIndexes.GetSize());
  int32_t tries = 100;
  while (tries-- > 0) {
    for (int32_t l = 0; l < ambiguousIndexCount.GetSize(); l++) {
      codewords[ambiguousIndexes[l]] =
          ambiguousIndexValues.GetAt(l)->GetAt(ambiguousIndexCount[l]);
    }
    CBC_CommonDecoderResult* decoderResult =
        decodeCodewords(codewords, ecLevel, erasureArray, e);
    if (e == BCExceptionNO)
      return decoderResult;

    e = BCExceptionNO;
    if (ambiguousIndexCount.GetSize() == 0) {
      e = BCExceptionChecksumInstance;
      return nullptr;
    }
    for (int32_t i = 0; i < ambiguousIndexCount.GetSize(); i++) {
      if (ambiguousIndexCount[i] <
          ambiguousIndexValues.GetAt(i)->GetSize() - 1) {
        ambiguousIndexCount[i]++;
        break;
      } else {
        ambiguousIndexCount[i] = 0;
        if (i == ambiguousIndexCount.GetSize() - 1) {
          e = BCExceptionChecksumInstance;
          return nullptr;
        }
      }
    }
  }
  e = BCExceptionChecksumInstance;
  return nullptr;
}
CBC_BarcodeValueArrayArray* CBC_PDF417ScanningDecoder::createBarcodeMatrix(
    CBC_DetectionResult* detectionResult) {
  CBC_BarcodeValueArrayArray* barcodeMatrix = new CBC_BarcodeValueArrayArray;
  barcodeMatrix->SetSize(detectionResult->getBarcodeRowCount());
  for (int32_t row = 0; row < barcodeMatrix->GetSize(); row++) {
    CBC_BarcodeValueArray* temp = new CBC_BarcodeValueArray;
    temp->SetSize(detectionResult->getBarcodeColumnCount() + 2);
    for (int32_t column = 0;
         column < detectionResult->getBarcodeColumnCount() + 2; column++) {
      temp->SetAt(column, new CBC_BarcodeValue());
    }
    barcodeMatrix->SetAt(row, temp);
  }
  for (int32_t i = 0;
       i < detectionResult->getDetectionResultColumns().GetSize(); i++) {
    CBC_DetectionResultColumn* detectionResultColumn =
        (CBC_DetectionResultColumn*)detectionResult->getDetectionResultColumns()
            .GetAt(i);
    if (!detectionResultColumn)
      continue;

    CFX_ArrayTemplate<CBC_Codeword*>* temp =
        detectionResultColumn->getCodewords();
    for (int32_t l = 0; l < temp->GetSize(); l++) {
      CBC_Codeword* codeword = temp->GetAt(l);
      if (!codeword || codeword->getRowNumber() == -1)
        continue;

      barcodeMatrix->GetAt(codeword->getRowNumber())
          ->GetAt(i)
          ->setValue(codeword->getValue());
    }
  }
  return barcodeMatrix;
}
FX_BOOL CBC_PDF417ScanningDecoder::isValidBarcodeColumn(
    CBC_DetectionResult* detectionResult,
    int32_t barcodeColumn) {
  return barcodeColumn >= 0 &&
         barcodeColumn <= detectionResult->getBarcodeColumnCount() + 1;
}
int32_t CBC_PDF417ScanningDecoder::getStartColumn(
    CBC_DetectionResult* detectionResult,
    int32_t barcodeColumn,
    int32_t imageRow,
    FX_BOOL leftToRight) {
  int32_t offset = leftToRight ? 1 : -1;
  CBC_Codeword* codeword = nullptr;
  if (isValidBarcodeColumn(detectionResult, barcodeColumn - offset)) {
    codeword = detectionResult->getDetectionResultColumn(barcodeColumn - offset)
                   ->getCodeword(imageRow);
  }
  if (codeword) {
    return leftToRight ? codeword->getEndX() : codeword->getStartX();
  }
  codeword = detectionResult->getDetectionResultColumn(barcodeColumn)
                 ->getCodewordNearby(imageRow);
  if (codeword) {
    return leftToRight ? codeword->getStartX() : codeword->getEndX();
  }
  if (isValidBarcodeColumn(detectionResult, barcodeColumn - offset)) {
    codeword = detectionResult->getDetectionResultColumn(barcodeColumn - offset)
                   ->getCodewordNearby(imageRow);
  }
  if (codeword) {
    return leftToRight ? codeword->getEndX() : codeword->getStartX();
  }
  int32_t skippedColumns = 0;
  while (isValidBarcodeColumn(detectionResult, barcodeColumn - offset)) {
    barcodeColumn -= offset;
    for (int32_t i = 0;
         i < detectionResult->getDetectionResultColumn(barcodeColumn)
                 ->getCodewords()
                 ->GetSize();
         i++) {
      CBC_Codeword* previousRowCodeword =
          (CBC_Codeword*)detectionResult->getDetectionResultColumn(
                                            barcodeColumn)
              ->getCodewords()
              ->GetAt(i);
      if (previousRowCodeword) {
        return (leftToRight ? previousRowCodeword->getEndX()
                            : previousRowCodeword->getStartX()) +
               offset * skippedColumns * (previousRowCodeword->getEndX() -
                                          previousRowCodeword->getStartX());
      }
    }
    skippedColumns++;
  }
  return leftToRight ? detectionResult->getBoundingBox()->getMinX()
                     : detectionResult->getBoundingBox()->getMaxX();
}
CBC_Codeword* CBC_PDF417ScanningDecoder::detectCodeword(
    CBC_CommonBitMatrix* image,
    int32_t minColumn,
    int32_t maxColumn,
    FX_BOOL leftToRight,
    int32_t startColumn,
    int32_t imageRow,
    int32_t minCodewordWidth,
    int32_t maxCodewordWidth) {
  startColumn = adjustCodewordStartColumn(image, minColumn, maxColumn,
                                          leftToRight, startColumn, imageRow);
  CFX_Int32Array* moduleBitCount = getModuleBitCount(
      image, minColumn, maxColumn, leftToRight, startColumn, imageRow);
  if (!moduleBitCount)
    return nullptr;

  int32_t endColumn;
  int32_t codewordBitCount = CBC_PDF417Common::getBitCountSum(*moduleBitCount);
  if (leftToRight) {
    endColumn = startColumn + codewordBitCount;
  } else {
    for (int32_t i = 0; i<moduleBitCount->GetSize()>> 1; i++) {
      int32_t tmpCount = moduleBitCount->GetAt(i);
      moduleBitCount->SetAt(
          i, moduleBitCount->GetAt(moduleBitCount->GetSize() - 1 - i));
      moduleBitCount->SetAt(moduleBitCount->GetSize() - 1 - i, tmpCount);
    }
    endColumn = startColumn;
    startColumn = endColumn - codewordBitCount;
  }
  int32_t decodedValue =
      CBC_PDF417CodewordDecoder::getDecodedValue(*moduleBitCount);
  int32_t codeword = CBC_PDF417Common::getCodeword(decodedValue);
  delete moduleBitCount;
  if (codeword == -1) {
    return nullptr;
  }
  return new CBC_Codeword(startColumn, endColumn,
                          getCodewordBucketNumber(decodedValue), codeword);
}
CFX_Int32Array* CBC_PDF417ScanningDecoder::getModuleBitCount(
    CBC_CommonBitMatrix* image,
    int32_t minColumn,
    int32_t maxColumn,
    FX_BOOL leftToRight,
    int32_t startColumn,
    int32_t imageRow) {
  int32_t imageColumn = startColumn;
  CFX_Int32Array* moduleBitCount = new CFX_Int32Array;
  moduleBitCount->SetSize(8);
  int32_t moduleNumber = 0;
  int32_t increment = leftToRight ? 1 : -1;
  FX_BOOL previousPixelValue = leftToRight;
  while (((leftToRight && imageColumn < maxColumn) ||
          (!leftToRight && imageColumn >= minColumn)) &&
         moduleNumber < moduleBitCount->GetSize()) {
    if (image->Get(imageColumn, imageRow) == previousPixelValue) {
      moduleBitCount->SetAt(moduleNumber,
                            moduleBitCount->GetAt(moduleNumber) + 1);
      imageColumn += increment;
    } else {
      moduleNumber++;
      previousPixelValue = !previousPixelValue;
    }
  }
  if (moduleNumber == moduleBitCount->GetSize() ||
      (((leftToRight && imageColumn == maxColumn) ||
        (!leftToRight && imageColumn == minColumn)) &&
       moduleNumber == moduleBitCount->GetSize() - 1)) {
    return moduleBitCount;
  }
  delete moduleBitCount;
  return nullptr;
}
int32_t CBC_PDF417ScanningDecoder::getNumberOfECCodeWords(
    int32_t barcodeECLevel) {
  return 2 << barcodeECLevel;
}
int32_t CBC_PDF417ScanningDecoder::adjustCodewordStartColumn(
    CBC_CommonBitMatrix* image,
    int32_t minColumn,
    int32_t maxColumn,
    FX_BOOL leftToRight,
    int32_t codewordStartColumn,
    int32_t imageRow) {
  int32_t correctedStartColumn = codewordStartColumn;
  int32_t increment = leftToRight ? -1 : 1;
  for (int32_t i = 0; i < 2; i++) {
    while (((leftToRight && correctedStartColumn >= minColumn) ||
            (!leftToRight && correctedStartColumn < maxColumn)) &&
           leftToRight == image->Get(correctedStartColumn, imageRow)) {
      if (abs(codewordStartColumn - correctedStartColumn) >
          CODEWORD_SKEW_SIZE) {
        return codewordStartColumn;
      }
      correctedStartColumn += increment;
    }
    increment = -increment;
    leftToRight = !leftToRight;
  }
  return correctedStartColumn;
}
FX_BOOL CBC_PDF417ScanningDecoder::checkCodewordSkew(int32_t codewordSize,
                                                     int32_t minCodewordWidth,
                                                     int32_t maxCodewordWidth) {
  return minCodewordWidth - CODEWORD_SKEW_SIZE <= codewordSize &&
         codewordSize <= maxCodewordWidth + CODEWORD_SKEW_SIZE;
}
CBC_CommonDecoderResult* CBC_PDF417ScanningDecoder::decodeCodewords(
    CFX_Int32Array& codewords,
    int32_t ecLevel,
    CFX_Int32Array& erasures,
    int32_t& e) {
  if (codewords.GetSize() == 0) {
    e = BCExceptionFormatInstance;
    return nullptr;
  }
  int32_t numECCodewords = 1 << (ecLevel + 1);
  correctErrors(codewords, erasures, numECCodewords, e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  verifyCodewordCount(codewords, numECCodewords, e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  CFX_ByteString bytestring;
  CBC_CommonDecoderResult* decoderResult = CBC_DecodedBitStreamPaser::decode(
      codewords, bytestring.FormatInteger(ecLevel), e);
  BC_EXCEPTION_CHECK_ReturnValue(e, nullptr);
  return decoderResult;
}
int32_t CBC_PDF417ScanningDecoder::correctErrors(CFX_Int32Array& codewords,
                                                 CFX_Int32Array& erasures,
                                                 int32_t numECCodewords,
                                                 int32_t& e) {
  if ((erasures.GetSize() != 0 &&
       erasures.GetSize() > (numECCodewords / 2 + MAX_ERRORS)) ||
      numECCodewords < 0 || numECCodewords > MAX_EC_CODEWORDS) {
    e = BCExceptionChecksumInstance;
    return -1;
  }
  int32_t result = CBC_PDF417ECErrorCorrection::decode(
      codewords, numECCodewords, erasures, e);
  BC_EXCEPTION_CHECK_ReturnValue(e, -1);
  return result;
}
void CBC_PDF417ScanningDecoder::verifyCodewordCount(CFX_Int32Array& codewords,
                                                    int32_t numECCodewords,
                                                    int32_t& e) {
  if (codewords.GetSize() < 4) {
    e = BCExceptionFormatInstance;
    return;
  }
  int32_t numberOfCodewords = codewords.GetAt(0);
  if (numberOfCodewords > codewords.GetSize()) {
    e = BCExceptionFormatInstance;
    return;
  }
  if (numberOfCodewords == 0) {
    if (numECCodewords < codewords.GetSize()) {
      codewords[0] = codewords.GetSize() - numECCodewords;
    } else {
      e = BCExceptionFormatInstance;
      return;
    }
  }
}
CFX_Int32Array* CBC_PDF417ScanningDecoder::getBitCountForCodeword(
    int32_t codeword) {
  CFX_Int32Array* result = new CFX_Int32Array;
  result->SetSize(8);
  int32_t previousValue = 0;
  int32_t i = result->GetSize() - 1;
  while (TRUE) {
    if ((codeword & 0x1) != previousValue) {
      previousValue = codeword & 0x1;
      i--;
      if (i < 0) {
        break;
      }
    }
    result->SetAt(i, result->GetAt(i) + 1);
    codeword >>= 1;
  }
  return result;
}
int32_t CBC_PDF417ScanningDecoder::getCodewordBucketNumber(int32_t codeword) {
  CFX_Int32Array* array = getBitCountForCodeword(codeword);
  int32_t result = getCodewordBucketNumber(*array);
  delete array;
  return result;
}
int32_t CBC_PDF417ScanningDecoder::getCodewordBucketNumber(
    CFX_Int32Array& moduleBitCount) {
  return (moduleBitCount.GetAt(0) - moduleBitCount.GetAt(2) +
          moduleBitCount.GetAt(4) - moduleBitCount.GetAt(6) + 9) %
         9;
}
