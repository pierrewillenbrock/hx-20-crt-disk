
#pragma once

#include <QDockWidget>
#include "hx20-ser-proto.hpp"
#include <deque>
#include <QAbstractItemModel>
#include <QDateTime>

QT_BEGIN_NAMESPACE

class QTreeView;
class QTimer;

QT_END_NAMESPACE

struct DecodeLocation {
    //the base is only valid during decode. later, the base is used as an
    //indication that the rest of the location is useful. When a decoder
    //inserts its own data, it must also make sure all derived
    //DecodeResult::location.base are set to nullptr
    uint8_t const *base;
    size_t begin;
    size_t end;
    uint8_t at(size_t pos) const { return base[begin+pos]; }
    size_t size() const { return end - begin; }
};

struct DecodeResult {
    std::vector<DecodeResult> subdecodes;
    QString name;
    QString value;
    DecodeLocation location;
};

struct RawDecodePacketInfo {
    enum Direction {
        MasterToSlave,
        SlaveToMaster
    };
    Direction dir;
    std::vector<uint8_t> raw;
    std::vector<DecodeResult> decoded;
    QDateTime time;
    QString title;
    QString info;
};

class Decoder;

class PacketListModel : public QAbstractItemModel {
private:
    std::deque<RawDecodePacketInfo> &packets;
public:
    PacketListModel(std::deque<RawDecodePacketInfo> &packets);
    void beforeAddPackets();
    void afterAddPackets();
protected:
    virtual QVariant data(const QModelIndex &index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    virtual QModelIndex parent(const QModelIndex &child) const override;
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;

    friend class CommsDebugWindow;
};

class PacketDecodeModel : public QAbstractItemModel {
private:
    struct TreeInfo {
        unsigned int parent;
        unsigned int indexInParent;
        std::vector<int> children;
    };

    std::deque<RawDecodePacketInfo> &packets;
    int pktidx;
    mutable std::vector<TreeInfo> treeInfo;
    DecodeResult const &findDecodeResult(QModelIndex const &idx) const;
public:
    PacketDecodeModel(std::deque<RawDecodePacketInfo> &packets);
    void setPacketIndex(int pktidx);
protected:
    virtual QVariant data(const QModelIndex &index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    virtual QModelIndex parent(const QModelIndex &child) const override;
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;

    friend class CommsDebugWindow;
};

class RawDecodeModel : public QAbstractItemModel {
private:
    std::deque<RawDecodePacketInfo> &packets;
    int pktidx;
    int selbegin;
    int selend;
public:
    RawDecodeModel(std::deque<RawDecodePacketInfo> &packets);
    void setPacketIndex(int pktidx);
    void selectBytes(int begin, int end);
protected:
    virtual QVariant data(const QModelIndex &index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    virtual QModelIndex parent(const QModelIndex &child) const override;
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;

    friend class CommsDebugWindow;
};

class CommsDebugWindow : public QDockWidget, public HX20SerialMonitor {
    Q_OBJECT;
private:
    std::deque<RawDecodePacketInfo> packets;
    std::deque<RawDecodePacketInfo> insertpackets;
    HX20SerialConnection *conn;
    bool scrollToNewest;

    QTreeView *packetlist;
    QTreeView *packetdecode;
    QTreeView *rawdecode;
    QTimer *insertTimer;
    PacketListModel *packetlistmodel;
    PacketDecodeModel *packetdecodemodel;
    RawDecodeModel *rawdecodemodel;
    std::unique_ptr<Decoder> rawDecoder;
protected:
    virtual void hideEvent(QHideEvent *event) override;
    virtual void showEvent(QShowEvent *event) override;

    virtual void monitorInput(InputPacketState state,
                              std::vector<uint8_t> const &bytes) override;
    virtual void monitorOutput(OutputPacketState state,
                               std::vector<uint8_t> const &bytes) override;

public:
    CommsDebugWindow(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~CommsDebugWindow();
    void setConnection(HX20SerialConnection *conn);
};
