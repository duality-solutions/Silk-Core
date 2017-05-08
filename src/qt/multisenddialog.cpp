#include "multisenddialog.h"
#include "ui_multisenddialog.h"

#include "addressbookpage.h"
#include "walletmodel.h"

#include "base58.h"
#include "init.h"
#include "wallet/wallet.h"

#include <QMessageBox>
#include <QLineEdit>

#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

extern CWallet* pwalletMain;

MultiSendDialog::MultiSendDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MultiSendDialog),
    model(0)
{
    ui->setupUi(this);
}

MultiSendDialog::~MultiSendDialog()
{
    delete ui;
}

void MultiSendDialog::setModel(WalletModel *model)
{
    this->model = model;
}

void MultiSendDialog::setAddress(const QString &address)
{
    setAddress(address, ui->multiSendAddressEdit);
}

void MultiSendDialog::setAddress(const QString &address, QLineEdit *addrEdit)
{
    addrEdit->setText(address);
    addrEdit->setFocus();
}

void MultiSendDialog::on_addressBookButton_clicked()
{
    if (model && model->getAddressTableModel())
    {
        AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec())
            setAddress(dlg.getReturnValue(), ui->multiSendAddressEdit);
    }
}

void MultiSendDialog::on_viewButton_clicked()
{
    std::pair<std::string, int> pMultiSend;
    std::string strMultiSendPrint = "";
    if(pwalletMain->fMultiSend)
        strMultiSendPrint += "<b>MultiSend Active</b><br>";
    else
        strMultiSendPrint += "<b>MultiSend Not Active</b><br>";
    for(int i = 0; i < (int)pwalletMain->vMultiSend.size(); i++)
    {
        pMultiSend = pwalletMain->vMultiSend[i];
        strMultiSendPrint += pMultiSend.first.c_str();
        strMultiSendPrint += " - ";
        strMultiSendPrint += boost::lexical_cast<string>(pMultiSend.second);
        strMultiSendPrint += "% \n";
    }
    ui->message->setProperty("status", "ok");
    ui->message->style()->polish(ui->message);
    ui->message->setText(QString(strMultiSendPrint.c_str()));
    return;
}

void MultiSendDialog::on_addButton_clicked()
{
    bool fValidConversion = false;
    std::string strAddress = ui->multiSendAddressEdit->text().toStdString();
    if (!CSequenceAddress(strAddress).IsValid())
    {
        ui->message->setProperty("status", "error");
        ui->message->style()->polish(ui->message);
        ui->message->setText(tr("The entered address:\n") + ui->multiSendAddressEdit->text() + tr(" is invalid.\nPlease check the address and try again."));
        ui->multiSendAddressEdit->setFocus();
        return;
    }
    int nMultiSendPercent = ui->multiSendPercentEdit->text().toInt(&fValidConversion, 10);
    int nSumMultiSend = 0;
    for(int i = 0; i < (int)pwalletMain->vMultiSend.size(); i++)
        nSumMultiSend += pwalletMain->vMultiSend[i].second;
    if(nSumMultiSend + nMultiSendPercent > 100)
    {
        ui->message->setProperty("status", "error");
        ui->message->style()->polish(ui->message);
        ui->message->setText(tr("The listed MultiSends\n are over 100% of your stake reward\n"));
        ui->multiSendAddressEdit->setFocus();
        return;
    }
    if (!fValidConversion || nMultiSendPercent > 100 || nMultiSendPercent <= 0)
    {
        ui->message->setProperty("status", "error");
        ui->message->style()->polish(ui->message);
        ui->message->setText(tr("Please Enter 1 - 100 for percent."));
        ui->multiSendPercentEdit->setFocus();
        return;
    }
    std::pair<std::string, int> pMultiSend;
    pMultiSend.first = strAddress;
    pMultiSend.second = nMultiSendPercent;
    pwalletMain->vMultiSend.push_back(pMultiSend);
    ui->message->setProperty("status", "ok");
    ui->message->style()->polish(ui->message);
    std::string strMultiSendPrint = "";
    for(int i = 0; i < (int)pwalletMain->vMultiSend.size(); i++)
    {
        pMultiSend = pwalletMain->vMultiSend[i];
        strMultiSendPrint += pMultiSend.first.c_str();
        strMultiSendPrint += " - ";
        strMultiSendPrint += boost::lexical_cast<string>(pMultiSend.second);
        strMultiSendPrint += "% <br>";
    }
    CWalletDB walletdb(pwalletMain->strWalletFile);
    walletdb.WriteMultiSend(pwalletMain->vMultiSend);
    ui->message->setText(tr("<b>MultiSend List</b><br>") + QString(strMultiSendPrint.c_str()));
    return;
}

void MultiSendDialog::on_deleteButton_clicked()
{
    std::vector<std::pair<std::string, int> > vMultiSendTemp = pwalletMain->vMultiSend;
    std::string strAddress = ui->multiSendAddressEdit->text().toStdString();
    bool fRemoved = false;
    for(int i = 0; i < (int)pwalletMain->vMultiSend.size(); i++)
    {
        if(pwalletMain->vMultiSend[i].first == strAddress)
        {
            pwalletMain->vMultiSend.erase(pwalletMain->vMultiSend.begin() + i);
            fRemoved = true;
        }
    }
    CWalletDB walletdb(pwalletMain->strWalletFile);
    if(!walletdb.EraseMultiSend(vMultiSendTemp))
        fRemoved = false;
    if(!walletdb.WriteMultiSend(pwalletMain->vMultiSend))
        fRemoved = false;
    if(fRemoved)
        ui->message->setText(tr("Removed ") + QString(strAddress.c_str()));
    else
        ui->message->setText(tr("Could not locate address\n"));
    return;
}

void MultiSendDialog::on_activateButton_clicked()
{
    std::string strRet = "";
    if(pwalletMain->vMultiSend.size() < 1)
        strRet = "Unable to activate MultiSend, check 'MultiSend List'\n";
    else if(CSequenceAddress(pwalletMain->vMultiSend[0].first).IsValid())
    {
        pwalletMain->fMultiSend = true;
        CWalletDB walletdb(pwalletMain->strWalletFile);
        if(!walletdb.WriteMSettings(true, pwalletMain->nLastMultiSendHeight))
            strRet = "MultiSend activated but writing settings to DB failed";
        else
            strRet = "<b>MultiSend activated</b>";
    }
    else
        strRet = "First Address Not Valid";
    ui->message->setProperty("status", "ok");
    ui->message->style()->polish(ui->message);
    ui->message->setText(tr(strRet.c_str()));
    return;
}

void MultiSendDialog::on_disableButton_clicked()
{
    std::string strRet = "";
    pwalletMain->fMultiSend = false;
    CWalletDB walletdb(pwalletMain->strWalletFile);
    if(!walletdb.WriteMSettings(false, pwalletMain->nLastMultiSendHeight))
        strRet = "MultiSend deactivated but writing settings to DB failed";
    else
        strRet = "<b>MultiSend deactivated</b>";
    ui->message->setProperty("status", "");
    ui->message->style()->polish(ui->message);
    ui->message->setText(tr(strRet.c_str()));
    return;
}