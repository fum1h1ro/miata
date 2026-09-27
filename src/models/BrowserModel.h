#ifndef MODELS_BROWSER_MODEL_H__
#define MODELS_BROWSER_MODEL_H__

#include "FileEntryModel.h"
#include "FileListModel.h"

namespace miata::models {
    class BrowserModel {
    public:
        static inline BrowserModel& Instance()
        {
            static BrowserModel instance;
            return instance;
        }

        inline FileListModel& Left()
        {
            return left_;
        }

        inline FileListModel& Right()
        {
            return right_;
        }


    private:
        BrowserModel();
        virtual ~BrowserModel();

        FileListModel left_;
        FileListModel right_;

    };
}

#endif // MODELS_BROWSER_MODEL_H__
