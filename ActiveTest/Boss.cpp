#include "Boss.h"

#include <QTime>
#include <QSettings>
#include <QGuiApplication>

#include <SMSObjectManager.h>
#include <MessageHolder.h>

#include <servertest.h>

Boss::Boss(QObject *parent)
  : QObject(parent)
  , smsObjectManager_(nullptr)
  , isConfirmed_(false)
  , unshownMessages_(nullptr)
  , serverTest_(new ServerTest(parent))
{
  QObject::connect(serverTest_, SIGNAL(emptyXml()), this, SLOT(onEmptyXml()));
  QObject::connect(serverTest_, SIGNAL(emptyMessageXml()), this, SLOT(onEmptyMessageXml()));
  QObject::connect(serverTest_, &ServerTest::serviceMessage, this, &Boss::onServiceMessageReceived);
  QObject::connect(serverTest_, &ServerTest::userMessage, this, &Boss::onUserMessageReceived);
}

Boss::~Boss()
{
  //sms object removes by parent

  if (unshownMessages_)
  {
    delete unshownMessages_;
    unshownMessages_ = nullptr;
  }
}

bool Boss::init()
{
  try
  {
    unshownMessages_ = new MessageHolder();
    smsObjectManager_ = new SMSObjectManager(this);
  }
  catch (std::bad_alloc&)
  {
    qWarning() << "Fatal error: unable to create SMSObjectManager or MessageHolder;";
    return false;
  }

  if (!smsObjectManager_->init())
  {
    qWarning() << "Fatal error: unable to init SMSObjectManager;";
    return false;
  }

  QObject::connect(smsObjectManager_, SIGNAL(titleCheck(bool)), this, SLOT(onTitleCheck(bool)));
  QObject::connect(smsObjectManager_, SIGNAL(messageSet()), this, SLOT(onMessageSet()));
  QObject::connect(smsObjectManager_, SIGNAL(messageDone()), this, SLOT(onMessageDone()));
  QObject::connect(smsObjectManager_, SIGNAL(messageFailed()), this, SLOT(onMessageFailed()));
  smsObjectManager_->start();

  return true;
}

void Boss::onTitleCheck(bool isTitleAlive)
{
  if (isTitleAlive)
  {
    if (!isConfirmed_)
    {
      isConfirmed_ = true;

      //TODO send confirmation to server 1 time
      serverTest_->emptyXmlRequest();
    }
  }
  else
  {
    qDebug() << "title is dead";
    isConfirmed_ = false;
    //TODO is server need to know this?
  }

  titleActive(isTitleAlive);
}

void Boss::onMessageSet()
{
  //TODO send message confirmation to server
  serverTest_->messageSetConfirm();

  MessageInfo message = unshownMessages_->first();
  messageChanged(message.id, message.text, message.priority);
}

void Boss::onMessageDone()
{
  unshownMessages_->dequeue();
  if (!unshownMessages_->isEmpty())
  {
    showNextMessage();
  }
  else
  {
    //TODO message request
    serverTest_->userMessageRequest();
  }
}

void Boss::onMessageFailed()
{
  qWarning() << "failed to set message, will try again on message callback";
}

void Boss::onEmptyXml()
{
  if (!unshownMessages_->isEmpty())
  {
    qWarning() << "some messages was not shown, show them first";
    showNextMessage();
  }
  else
  {
    //TODO message request
    serverTest_->userMessageRequest();
  }
}

void Boss::onEmptyMessageXml()
{
  //TODO service message request
  serverTest_->serviceMessageRequest();
}

void Boss::onUserMessageReceived(long id, const QByteArray &message, const QTime &time, long priority)
{
  //addSplittedMessage(id, message, time, priority);
  unshownMessages_->add(id, message, time, priority);
  showNextMessage();
}

void Boss::onServiceMessageReceived(long id, const QByteArray &message, const QTime &time)
{
  //addSplittedMessage(id, message, time);
  unshownMessages_->add(id, message, time, 0);
  showNextMessage();
}

void Boss::showNextMessage()
{
  if (unshownMessages_->isEmpty())
  {
    qWarning() << "empty message queue";
    //TODO request message
    serverTest_->userMessageRequest();
    messageChanged(-1, "", -1);
    return;
  }

  MessageInfo message = unshownMessages_->first();
  smsObjectManager_->setMessage(message.id, message.text, message.priority);

  QSettings settings(QGuiApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
  bool isMobileUsing = settings.value("Mobile/use").toBool();
  if (isMobileUsing)
  {
    qDebug() << "mobile app is using";
    //TODO send to mobile app
  }
}

void Boss::addSplittedMessage(long id, const QByteArray &message, const QTime &time, long priority /* = 0*/)
{
  QList<QByteArray> spaceSplitted = message.split(' ');
  QByteArray newMessage = formMessage(&spaceSplitted, id, priority);
  while (newMessage.size() != 0)
  {
    unshownMessages_->add(id, newMessage, time, priority);
    newMessage = formMessage(&spaceSplitted, id, priority);
  }
}

QByteArray Boss::formMessage(QList<QByteArray> *splittedMessage, long id, long priority)
{
  QByteArray newMessage, test;
  while (!splittedMessage->empty())
  {
    test.append(splittedMessage->first());
    if (smsObjectManager_->testMessage(id, test, priority))
    {
      newMessage.append(splittedMessage->first());
      newMessage.append(' ');
      test.append(' ');
      splittedMessage->pop_front();
    }
    else
    {
      if (newMessage.size() == 0)
      {
        //TODO split by letters
      }
      break;
    }
  }
  return newMessage;
}
