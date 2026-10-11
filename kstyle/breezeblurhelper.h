//////////////////////////////////////////////////////////////////////////////
// breezeblurhelper.h
// handle regions passed to kwin for blurring
// -------------------
//
// SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>
//
// Largely rewritten from Oxygen widget style
// SPDX-FileCopyrightText: 2007 Thomas Luebking <thomas.luebking@web.de>
// SPDX-FileCopyrightText: 2010 Hugo Pereira Da Costa <hugo.pereira@free.fr>
//
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "breeze.h"
#include "breezehelper.h"

#include <QHash>
#include <QObject>

namespace Breeze
{
class BlurHelper : public QObject
{
    Q_OBJECT

public:
    //! constructor
    explicit BlurHelper(const std::shared_ptr<Helper> &helper);

    //! register widget
    void registerWidget(QWidget *);

    //! register widget
    void unregisterWidget(QWidget *);

    //! watch the application's Qt Quick windows
    void registerApplication(QApplication *);

    //! stop watching the application's Qt Quick windows
    void unregisterApplication(QApplication *);

    //! event filter
    bool eventFilter(QObject *, QEvent *) override;

protected:
    //! install event filter to object, in a unique way
    void addEventFilter(QObject *object)
    {
        object->removeEventFilter(this);
        object->installEventFilter(this);
    }

    //! update blur regions for given widget
    void update(QWidget *) const;

private:
    //* asks KWin for glass behind Qt Quick application windows without a background
    class QuickWindowFilter : public QObject
    {
    public:
        explicit QuickWindowFilter(BlurHelper *parent)
            : QObject(parent)
            , _helper(parent->_helper)
        {
        }

        bool eventFilter(QObject *, QEvent *) override;

    private:
        std::shared_ptr<Helper> _helper;
    };

    std::shared_ptr<Helper> _helper;
    QuickWindowFilter *_quickWindowFilter = nullptr;
};

}
