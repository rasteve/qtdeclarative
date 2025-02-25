// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause
package com.example.qtabstractitemmodel_java;

import android.util.Log;

import org.qtproject.qt.android.QtAbstractItemModel;
import org.qtproject.qt.android.QtModelIndex;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Objects;

public class MyDataModel extends QtAbstractItemModel {
    // Representing a cell in the sheet
    private class Cell {
        private String m_value;
        public Cell(String value) {
            m_value = value;
        }
        public String getValue() { return m_value; }
        public void setValue(String value) {
            m_value = value;
        }
    }

    private static final String TAG = "QtAIM MyDataModel";
    private static final int ROLE_DISPLAY = 0;
    private static final int ROLE_EDIT = 1;
    private static final int MAX_ROWS_AND_COLUMNS = 26;
    private int m_columns = 4;
    /*
     * Two dimensional array of Cell objects to represent a sheet.
     * First dimension are rows. Second dimension are columns.
     * TODO QTBUG-127467
     */
    private final ArrayList<ArrayList<Cell>> m_dataList = new ArrayList<>();
    private final char m_firstLatinLetter = 'A';

    //! [1]
    /*
    * Initializes the two-dimensional array list with following content:
    * [] [] [] [] 1A 1B 1C 1D
    * [] [] [] [] 2A 2B 2C 2D
    * [] [] [] [] 3A 3B 3C 3D
    * [] [] [] [] 4A 4B 4C 4D
    * Threading: called in Android main thread context.
    */
    public MyDataModel() {
    //! [1]
        final int initializingRowAndColumnCount = m_columns;
        synchronized (this) {
            for (int rows = 0; rows < initializingRowAndColumnCount; rows++) {
                ArrayList<Cell> newRow = new ArrayList<>();
                for (int columns = 0; columns < initializingRowAndColumnCount; columns++) {
                    String column = String.valueOf((char) (m_firstLatinLetter + columns));
                    newRow.add(new Cell((rows + 1) + column));
                }
                m_dataList.add(newRow);
            }
        }
    }
    //! [2]
    /*
    * Returns the count of columns.
    * Threading: called in Android main thread context.
    * Threading: called in Qt qtMainLoopThread thread context.
    */
    @Override
    synchronized public int columnCount(QtModelIndex qtModelIndex) {
        return m_columns;
    }

    /*
    * Returns the count of rows.
    * Threading: called in Android main thread context.
    * Threading: called in Qt qtMainLoopThread thread context.
    */
    @Override
    synchronized public int rowCount(QtModelIndex qtModelIndex) {
        return m_dataList.size();
    }
    //! [2]

    //! [3]
    /*
    * Returns the data to QML based on the roleNames
    * Threading: called in Qt qtMainLoopThread thread context.
    */
    @Override
    synchronized public Object data(QtModelIndex qtModelIndex, int role) {
        if (role == ROLE_DISPLAY) {
            Cell elementForEdit = m_dataList.get(qtModelIndex.row()).get(qtModelIndex.column());
            return elementForEdit.getValue();
        }
        Log.w(TAG, "data(): unrecognized role: " + role);
        return null;
    }

    /*
    * Defines what string i.e. role in QML side gets the data from Java side.
    * Threading: called in Qt qtMainLoopThread thread context.
    */
    @Override
    synchronized public HashMap<Integer, String> roleNames() {
        HashMap<Integer, String> roles = new HashMap<>();
        roles.put(ROLE_DISPLAY, "display");
        roles.put(ROLE_EDIT, "edit");
        return roles;
    }

    /*
    * Returns a new index model.
    * Threading: called in Qt qtMainLoopThread thread context.
    */
    @Override
    synchronized public QtModelIndex index(int row, int column, QtModelIndex parent) {
        return createIndex(row, column, 0);
    }

    /*
    * Returns a parent model.
    * Threading: not used called in this example.
    */
    @Override
    synchronized public QtModelIndex parent(QtModelIndex qtModelIndex) {
        return new QtModelIndex();
    }
    //! [3]

    //! [4]
    /*
    * Gets called when model data is edited from QML side.
    * Sets the role data for the item at index to value,
    * if given index is valid and if data in given index truly changed.
    */
    @Override
    synchronized public boolean setData(QtModelIndex index, Object value, int role) {
        Cell cellAtIndex = m_dataList.get(index.row()).get(index.column());
        String cellValueAtIndex = cellAtIndex.getValue();
        if (!index.isValid() || role != ROLE_EDIT
                || Objects.equals(cellValueAtIndex, value.toString())) {
            return false;
        }
        cellAtIndex.setValue(value.toString());
        // Send dataChanged() when data was successfully set.
        dataChanged(index, index, new int[]{role});
        return true;
    }
    //! [4]

    //! [5]
    /*
    * Adds a row.
    * Threading: called in Android main thread context.
    */
    synchronized public void addRow() {
        if (m_columns > 0 && m_dataList.size() < MAX_ROWS_AND_COLUMNS) {
            beginInsertRows(new QtModelIndex(), m_dataList.size(), m_dataList.size());
            m_dataList.add(generateNewRow());
            endInsertRows();
        }
    }

    /*
    * Removes a row.
    * Threading: called in Android main thread context.
    */
    synchronized public void removeRow() {
        if (m_dataList.size() > 1) {
            beginRemoveRows(new QtModelIndex(), m_dataList.size() - 1, m_dataList.size() - 1);
            m_dataList.remove(m_dataList.size() - 1);
            endRemoveRows();
        }
    }
    //! [5]

    //! [6]
    /*
    * Adds a column.
    * Threading: called in Android main thread context.
    */
    synchronized public void addColumn() {
        if (!m_dataList.isEmpty() && m_columns < MAX_ROWS_AND_COLUMNS) {
            beginInsertColumns(new QtModelIndex(), m_columns, m_columns);
            generateNewColumn();
            m_columns += 1;
            endInsertColumns();
        }
    }

    /*
    * Removes a column.
    * Threading: called in Android main thread context.
    */
    synchronized public void removeColumn() {
        if (m_columns > 1) {
            int columnToRemove = m_columns - 1;
            beginRemoveColumns(new QtModelIndex(), columnToRemove, columnToRemove);
            for (int row = 0; row < m_dataList.size(); row++)
                m_dataList.get(row).remove(columnToRemove);
            m_columns -= 1;
            endRemoveColumns();
        }
    }
    //! [6]

    synchronized private void generateNewColumn() {
        int amountOfRows = m_dataList.size();
        ArrayList<Cell> lastRow = m_dataList.get(amountOfRows-1);
        Cell lastCellOfLastRow = lastRow.get(m_columns-1);
        String letterOfLastCell = lastCellOfLastRow.getValue().substring(1);
        // Next go through each row and add to each one of them a new column
        // If next char is not a letter we reached the end of amount of alphabets for that column
        int counter = 1;
        for (ArrayList<Cell> row : m_dataList) {
            String letterOfNewColumn = letterOfLastCell;
            int amountOfChars = letterOfNewColumn.length();
            char newColumnChar = letterOfNewColumn.charAt(amountOfChars-1);
            if (Character.isLetter(newColumnChar+1)) {
                newColumnChar = (char) (newColumnChar+1);
                letterOfNewColumn = String.valueOf((newColumnChar));
            }
            Cell newCell = new Cell(counter + letterOfNewColumn);
            row.add(newCell);
            counter++;
        }
    }

    synchronized private ArrayList<Cell> generateNewRow(){
        int amountOfRows = m_dataList.size();
        ArrayList<Cell> newRow = new ArrayList<Cell>();
        for (int count = 0; count < m_columns; count++) {
            String columnLetter = String.valueOf((char) (m_firstLatinLetter + count));
            Cell newCell = new Cell((amountOfRows + 1) + columnLetter);
            newRow.add(newCell);
        }
        return newRow;
    }
}
