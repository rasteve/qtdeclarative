// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qv4dateobject_p.h"
#include "qv4runtime_p.h"
#include "qv4symbol_p.h"

#include <QtCore/QDebug>
#include <QtCore/QDateTime>
#include <QtCore/private/qlocaltime_p.h>
#include <QtCore/QStringList>
#include <QtCore/QTimeZone>

#include <wtf/MathExtras.h>

using namespace QV4;

static const double HoursPerDay = 24.0;
static const double MinutesPerHour = 60.0;
static const double SecondsPerMinute = 60.0;
static const double msPerSecond = 1000.0;
static const double msPerMinute = 60000.0;
static const double msPerHour = 3600000.0;
static const double msPerDay = 86400000.0;

static inline double TimeWithinDay(double t)
{
    double r = ::fmod(t, msPerDay);
    return (r >= 0) ? r : r + msPerDay;
}

static inline int HourFromTime(double t)
{
    int r = int(::fmod(::floor(t / msPerHour), HoursPerDay));
    return (r >= 0) ? r : r + int(HoursPerDay);
}

static inline int MinFromTime(double t)
{
    int r = int(::fmod(::floor(t / msPerMinute), MinutesPerHour));
    return (r >= 0) ? r : r + int(MinutesPerHour);
}

static inline int SecFromTime(double t)
{
    int r = int(::fmod(::floor(t / msPerSecond), SecondsPerMinute));
    return (r >= 0) ? r : r + int(SecondsPerMinute);
}

static inline int msFromTime(double t)
{
    int r = int(::fmod(t, msPerSecond));
    return (r >= 0) ? r : r + int(msPerSecond);
}

static inline double Day(double t)
{
    return ::floor(t / msPerDay);
}

static inline double DaysInYear(double y)
{
    if (::fmod(y, 4))
        return 365;

    else if (::fmod(y, 100))
        return 366;

    else if (::fmod(y, 400))
        return 365;

    return 366;
}

static inline double DayFromYear(double y)
{
    return 365 * (y - 1970)
        + ::floor((y - 1969) / 4)
        - ::floor((y - 1901) / 100)
        + ::floor((y - 1601) / 400);
}

static inline double TimeFromYear(double y)
{
    return msPerDay * DayFromYear(y);
}

static inline double YearFromTime(double t)
{
    int y = 1970;
    y += (int) ::floor(t / (msPerDay * 365.2425));

    double t2 = TimeFromYear(y);
    return (t2 > t) ? y - 1 : ((t2 + msPerDay * DaysInYear(y)) <= t) ? y + 1 : y;
}

static inline bool InLeapYear(double t)
{
    double x = DaysInYear(YearFromTime(t));
    if (x == 365)
        return 0;

    Q_ASSERT(x == 366);
    return 1;
}

static inline double DayWithinYear(double t)
{
    return Day(t) - DayFromYear(YearFromTime(t));
}

static inline double MonthFromTime(double t)
{
    double d = DayWithinYear(t);
    double l = InLeapYear(t);

    if (d < 31.0)
        return 0;

    else if (d < 59.0 + l)
        return 1;

    else if (d < 90.0 + l)
        return 2;

    else if (d < 120.0 + l)
        return 3;

    else if (d < 151.0 + l)
        return 4;

    else if (d < 181.0 + l)
        return 5;

    else if (d < 212.0 + l)
        return 6;

    else if (d < 243.0 + l)
        return 7;

    else if (d < 273.0 + l)
        return 8;

    else if (d < 304.0 + l)
        return 9;

    else if (d < 334.0 + l)
        return 10;

    else if (d < 365.0 + l)
        return 11;

    return qt_qnan(); // ### assert?
}

static inline double DateFromTime(double t)
{
    int m = (int) QV4::Value::toInteger(MonthFromTime(t));
    double d = DayWithinYear(t);
    double l = InLeapYear(t);

    switch (m) {
    case 0: return d + 1.0;
    case 1: return d - 30.0;
    case 2: return d - 58.0 - l;
    case 3: return d - 89.0 - l;
    case 4: return d - 119.0 - l;
    case 5: return d - 150.0 - l;
    case 6: return d - 180.0 - l;
    case 7: return d - 211.0 - l;
    case 8: return d - 242.0 - l;
    case 9: return d - 272.0 - l;
    case 10: return d - 303.0 - l;
    case 11: return d - 333.0 - l;
    }

    return qt_qnan(); // ### assert
}

static inline double WeekDay(double t)
{
    double r = ::fmod (Day(t) + 4.0, 7.0);
    return (r >= 0) ? r : r + 7.0;
}


static inline double MakeTime(double hour, double min, double sec, double ms)
{
    if (!qIsFinite(hour) || !qIsFinite(min) || !qIsFinite(sec) || !qIsFinite(ms))
        return qQNaN();
    hour = QV4::Value::toInteger(hour);
    min = QV4::Value::toInteger(min);
    sec = QV4::Value::toInteger(sec);
    ms = QV4::Value::toInteger(ms);
    return ((hour * MinutesPerHour + min) * SecondsPerMinute + sec) * msPerSecond + ms;
}

static inline double DayFromMonth(double month, double leap)
{
    switch ((int) month) {
    case 0: return 0;
    case 1: return 31.0;
    case 2: return 59.0 + leap;
    case 3: return 90.0 + leap;
    case 4: return 120.0 + leap;
    case 5: return 151.0 + leap;
    case 6: return 181.0 + leap;
    case 7: return 212.0 + leap;
    case 8: return 243.0 + leap;
    case 9: return 273.0 + leap;
    case 10: return 304.0 + leap;
    case 11: return 334.0 + leap;
    }

    return qt_qnan(); // ### assert?
}

static double MakeDay(double year, double month, double day)
{
    if (!qIsFinite(year) || !qIsFinite(month) || !qIsFinite(day))
        return qQNaN();
    year = QV4::Value::toInteger(year);
    month = QV4::Value::toInteger(month);
    day = QV4::Value::toInteger(day);

    year += ::floor(month / 12.0);

    month = ::fmod(month, 12.0);
    if (month < 0)
        month += 12.0;

    /* Quoting the spec:

       Find a value t such that YearFromTime(t) is ym and MonthFromTime(t) is mn
       and DateFromTime(t) is 1; but if this is not possible (because some
       argument is out of range), return NaN.
    */
    double first = DayFromYear(year);
    /* Beware floating-point glitches: don't test the first millisecond of a
     * year, month or day when we could test a moment firmly in the interior of
     * the interval. A rounding glitch might give the first millisecond to the
     * preceding interval.
     */
    bool leap = InLeapYear((first + 60) * msPerDay);

    first += DayFromMonth(month, leap);
    const double t = first * msPerDay + msPerDay / 2; // Noon on the first of the month
    Q_ASSERT(Day(t) == first);
    if (YearFromTime(t) != year || MonthFromTime(t) != month || DateFromTime(t) != 1) {
        qWarning("Apparently out-of-range date %.0f-%02.0f-%02.0f", year, month, day);
        return qt_qnan();
    }
    return first + day - 1;
}

static inline double MakeDate(double day, double time)
{
    return day * msPerDay + time;
}

/*
  ECMAScript specifies use of a fixed (current, standard) time-zone offset,
  LocalTZA; and LocalTZA + DaylightSavingTA(t) is taken to be (see LocalTime and
  UTC, following) local time's offset from UTC at time t.  For simple zones,
  DaylightSavingTA(t) is thus the DST offset applicable at date/time t; however,
  if a zone has changed its standard offset, the only way to make LocalTime and
  UTC (if implemented in accord with the spec) perform correct transformations
  is to have DaylightSavingTA(t) correct for the zone's standard offset change
  as well as its actual DST offset.

  This means we have to treat any historical changes in the zone's standard
  offset as DST perturbations, regardless of historical reality.  (This shall
  mean a whole day of DST offset for some zones, that have crossed the
  international date line.  This shall confuse client code.)  The bug report
  against the ECMAScript spec is https://github.com/tc39/ecma262/issues/725
  and they've now changed the spec so that the following conforms to it ;^>
*/
static inline double DaylightSavingTA(double t, double localTZA) // t is a UTC time
{
    return QLocalTime::getUtcOffset(qint64(t)) * 1e3 - localTZA;
}

static inline double LocalTime(double t, double localTZA)
{
    // Flawed, yet verbatim from the spec:
    return t + localTZA + DaylightSavingTA(t, localTZA);
}

// The spec does note [*] that UTC and LocalTime are not quite mutually inverse.
// [*] http://www.ecma-international.org/ecma-262/7.0/index.html#sec-utc-t

static inline double UTC(double t, double localTZA)
{
    // Flawed, yet verbatim from the spec:
    return t - localTZA - DaylightSavingTA(t - localTZA, localTZA);
}

static inline double currentTime()
{
    return QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
}

static inline double TimeClip(double t)
{
    if (!qt_is_finite(t) || fabs(t) > Date::MaxDateVal)
        return qt_qnan();

    // +0 looks weird, but is correct. See ES6 20.3.1.15. We must not return -0.
    return QV4::Value::toInteger(t) + 0;
}

static inline double ParseString(const QString &s, double localTZA)
{
    /*
      First, try the format defined in ECMA 262's "Date Time String Format";
      only if that fails, fall back to QDateTime for parsing

      The defined string format is yyyy-MM-ddTHH:mm:ss.zzzt; the time (T and all
      after it) may be omitted. In each part, the second and later components
      are optional. There's an extended syntax for negative and large positive
      years: ±yyyyyy; the leading sign, even when +, isn't optional.  If month
      (MM) or day (dd) is omitted, it is 01; if minute (mm) or second (ss) is
      omitted, it's 00; if milliseconds (zzz) are omitted, they're 000.

      When the time zone offset (t) is absent, date-only forms are interpreted as
      indicating a UTC time and date-time forms are interpreted in local time.
    */

    enum Format {
        Year,
        Month,
        Day,
        Hour,
        Minute,
        Second,
        MilliSecond,
        TimezoneHour,
        TimezoneMinute,
        Done
    };

    const QChar *ch = s.constData();
    const QChar *end = ch + s.size();

    uint format = Year;
    int current = 0;
    int currentSize = 0;
    bool extendedYear = false;

    int yearSign = 1;
    int year = 0;
    int month = 0;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int msec = 0;
    int offsetSign = 1;
    int offset = 0;
    bool seenT = false;
    bool seenZ = false; // Have seen zone, i.e. +HH:mm or literal Z.

    bool error = false;
    if (*ch == u'+' || *ch == u'-') {
        extendedYear = true;
        if (*ch == u'-')
            yearSign = -1;
        ++ch;
    }
    for (; ch <= end && !error && format != Done; ++ch) {
        if (*ch >= u'0' && *ch <= u'9') {
            current *= 10;
            current += ch->unicode() - u'0';
            ++currentSize;
        } else { // other char, delimits field
            switch (format) {
            case Year:
                year = current;
                if (extendedYear)
                    error = (currentSize != 6);
                else
                    error = (currentSize != 4);
                break;
            case Month:
                month = current - 1;
                error = (currentSize != 2) || month > 11;
                break;
            case Day:
                day = current;
                error = (currentSize != 2) || day > 31;
                break;
            case Hour:
                hour = current;
                error = (currentSize != 2) || hour > 24;
                break;
            case Minute:
                minute = current;
                error = (currentSize != 2) || minute >= 60;
                break;
            case Second:
                second = current;
                error = (currentSize != 2) || second > 60;
                break;
            case MilliSecond:
                msec = current;
                error = (currentSize != 3);
                break;
            case TimezoneHour:
                Q_ASSERT(offset == 0 && !seenZ);
                offset = current * 60;
                error = (currentSize != 2) || current > 23;
                seenZ = true;
                break;
            case TimezoneMinute:
                offset += current;
                error = (currentSize != 2) || current >= 60;
                break;
            }
            if (*ch == u'T') {
                if (format >= Hour)
                    error = true;
                format = Hour;
                seenT = true;
            } else if (*ch == u'-') {
                if (format < Day)
                    ++format;
                else if (format < Minute)
                    error = true;
                else if (format >= TimezoneHour)
                    error = true;
                else {
                    Q_ASSERT(offset == 0 && !seenZ);
                    offsetSign = -1;
                    format = TimezoneHour;
                }
            } else if (*ch == u':') {
                if (format != Hour && format != Minute && format != TimezoneHour)
                    error = true;
                ++format;
            } else if (*ch == u'.') {
                if (format != Second)
                    error = true;
                ++format;
            } else if (*ch == u'+') {
                if (seenZ || format < Minute || format >= TimezoneHour)
                    error = true;
                format = TimezoneHour;
            } else if (*ch == u'Z') {
                if (seenZ || format < Minute || format >= TimezoneHour)
                    error = true;
                else
                    Q_ASSERT(offset == 0);
                format = Done;
                seenZ = true;
            } else if (ch->unicode() == 0) {
                format = Done;
            }
            current = 0;
            currentSize = 0;
        }
    }

    if (!error) {
        double t = MakeDate(MakeDay(year * yearSign, month, day), MakeTime(hour, minute, second, msec));
        if (seenZ)
            t -= offset * offsetSign * 60 * 1000;
        else if (seenT) // No zone specified, treat date-time as local time
            t = UTC(t, localTZA);
        // else: treat plain date as already in UTC
        return TimeClip(t);
    }

    QDateTime dt = QDateTime::fromString(s, Qt::TextDate);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::RFC2822Date);
    if (!dt.isValid()) {
        const QString formats[] = {
            QStringLiteral("M/d/yyyy"),
            QStringLiteral("M/d/yyyy hh:mm"),
            QStringLiteral("M/d/yyyy hh:mm A"),

            QStringLiteral("M/d/yyyy, hh:mm"),
            QStringLiteral("M/d/yyyy, hh:mm A"),

            QStringLiteral("MMM d yyyy"),
            QStringLiteral("MMM d yyyy hh:mm"),
            QStringLiteral("MMM d yyyy hh:mm:ss"),
            QStringLiteral("MMM d yyyy, hh:mm"),
            QStringLiteral("MMM d yyyy, hh:mm:ss"),

            QStringLiteral("MMMM d yyyy"),
            QStringLiteral("MMMM d yyyy hh:mm"),
            QStringLiteral("MMMM d yyyy hh:mm:ss"),
            QStringLiteral("MMMM d yyyy, hh:mm"),
            QStringLiteral("MMMM d yyyy, hh:mm:ss"),

            QStringLiteral("MMM d, yyyy"),
            QStringLiteral("MMM d, yyyy hh:mm"),
            QStringLiteral("MMM d, yyyy hh:mm:ss"),

            QStringLiteral("MMMM d, yyyy"),
            QStringLiteral("MMMM d, yyyy hh:mm"),
            QStringLiteral("MMMM d, yyyy hh:mm:ss"),

            QStringLiteral("d MMM yyyy"),
            QStringLiteral("d MMM yyyy hh:mm"),
            QStringLiteral("d MMM yyyy hh:mm:ss"),
            QStringLiteral("d MMM yyyy, hh:mm"),
            QStringLiteral("d MMM yyyy, hh:mm:ss"),

            QStringLiteral("d MMMM yyyy"),
            QStringLiteral("d MMMM yyyy hh:mm"),
            QStringLiteral("d MMMM yyyy hh:mm:ss"),
            QStringLiteral("d MMMM yyyy, hh:mm"),
            QStringLiteral("d MMMM yyyy, hh:mm:ss"),

            QStringLiteral("d MMM, yyyy"),
            QStringLiteral("d MMM, yyyy hh:mm"),
            QStringLiteral("d MMM, yyyy hh:mm:ss"),

            QStringLiteral("d MMMM, yyyy"),
            QStringLiteral("d MMMM, yyyy hh:mm"),
            QStringLiteral("d MMMM, yyyy hh:mm:ss"),

            // ISO 8601 and RFC 2822 with a GMT as prefix on its offset, or GMT as zone.
            QStringLiteral("yyyy-MM-dd hh:mm:ss t"),
            QStringLiteral("ddd, d MMM yyyy hh:mm:ss t"),
        };

        for (const QString &format : formats) {
            dt = format.indexOf(QLatin1String("hh:mm")) < 0
                    ? QDate::fromString(s, format).startOfDay(QTimeZone::UTC)
                    : QDateTime::fromString(s, format); // as local time
            if (dt.isValid())
                break;
        }
    }
    if (!dt.isValid())
        return qt_qnan();
    return TimeClip(dt.toMSecsSinceEpoch());
}

/*!
  \internal

  Converts the ECMA Date value \a t (in UTC form) to QDateTime
  according to \a spec.
*/
static inline QDateTime ToDateTime(double t, QTimeZone zone)
{
    if (std::isnan(t))
        return QDateTime().toTimeZone(zone);
    return QDateTime::fromMSecsSinceEpoch(t, zone);
}

static inline QString ToString(double t, double localTZA)
{
    if (std::isnan(t))
        return QStringLiteral("Invalid Date");
    QString str = ToDateTime(t, QTimeZone::LocalTime).toString() + QLatin1String(" GMT");
    double tzoffset = localTZA + DaylightSavingTA(t, localTZA);
    if (tzoffset) {
        int hours = static_cast<int>(::fabs(tzoffset) / 1000 / 60 / 60);
        int mins = int(::fabs(tzoffset) / 1000 / 60) % 60;
        str.append(QLatin1Char((tzoffset > 0) ?  '+' : '-'));
        if (hours < 10)
            str.append(QLatin1Char('0'));
        str.append(QString::number(hours));
        if (mins < 10)
            str.append(QLatin1Char('0'));
        str.append(QString::number(mins));
    }
    return str;
}

static inline QString ToUTCString(double t)
{
    if (std::isnan(t))
        return QStringLiteral("Invalid Date");
    return ToDateTime(t, QTimeZone::UTC).toString();
}

static inline QString ToDateString(double t)
{
    return ToDateTime(t, QTimeZone::LocalTime).date().toString();
}

static inline QString ToTimeString(double t)
{
    return ToDateTime(t, QTimeZone::LocalTime).time().toString();
}

static inline QString ToLocaleString(double t)
{
    return QLocale().toString(ToDateTime(t, QTimeZone::LocalTime), QLocale::ShortFormat);
}

static inline QString ToLocaleDateString(double t)
{
    return QLocale().toString(ToDateTime(t, QTimeZone::LocalTime).date(), QLocale::ShortFormat);
}

static inline QString ToLocaleTimeString(double t)
{
    return QLocale().toString(ToDateTime(t, QTimeZone::LocalTime).time(), QLocale::ShortFormat);
}

static double getLocalTZA()
{
    return QLocalTime::getCurrentStandardUtcOffset() * 1e3;
}

DEFINE_OBJECT_VTABLE(DateObject);

quint64 Date::encode(double value)
{
    if (std::isnan(value) || fabs(value) > MaxDateVal)
        return InvalidDateVal;

    // Do the addition in qint64. This way we won't overflow if value is negative
    // and we will round value in the right direction.
    // We know we can do this because we know we have more than one bit left in quint64.
    const quint64 encoded = quint64(qint64(value) + qint64(MaxDateVal));

    return encoded + Extra;
}

quint64 Date::encode(const QDateTime &dateTime)
{
    return encode(dateTime.isValid() ? dateTime.toMSecsSinceEpoch() : qt_qnan());
}

void Date::init(double value)
{
    storage = encode(value);
}

void Date::init(const QDateTime &when)
{
    storage = encode(when) | HasQDate | HasQTime;
}

void Date::init(QDate date)
{
    storage = encode(date.startOfDay(QTimeZone::UTC)) | HasQDate;
}

void Date::init(QTime time, ExecutionEngine *engine)
{
    if (!time.isValid()) {
        storage = encode(qt_qnan()) | HasQTime;
        return;
    }

    /* We have to chose a date on which to instantiate this time.  All we really
     * care about is that it round-trips back to the same time if we extract the
     * time from it, which shall (via toQDateTime(), below) discard the date
     * part.  We need a date for which time-zone data is likely to be sane (so
     * MakeDay(0, 0, 0) was a bad choice; 2 BC, December 31st is before
     * time-zones were standardized), with no transition nearby in date.
     * QDateTime ignores DST transitions before 1970, but even then zone
     * transitions did happen; and DaylightSavingTA() will include DST, at odds
     * with QDateTime.  So pick a date since 1970 and prefer one when no zone
     * was in DST.  One such interval (according to the Olson database, at
     * least) was 1971 March 15th to April 17th.  Since converting a time to a
     * date-time without specifying a date is foolish, let's use April Fools'
     * day.
     */
    static const double d = MakeDay(1971, 3, 1);
    double t = MakeTime(time.hour(), time.minute(), time.second(), time.msec());
    storage = encode(UTC(MakeDate(d, t), engine->localTZA)) | HasQTime;
}

QDate Date::toQDate() const
{
    return toQDateTime().date();
}

QTime Date::toQTime() const
{
    return toQDateTime().time();
}

QDateTime Date::toQDateTime() const
{
    return ToDateTime(operator double(), QTimeZone::LocalTime);
}

QVariant Date::toVariant() const
{
    // Note that we shouldn't and don't read-back here, compared to
    // most other methods, as this is only used when we perform a
    // write-back, that is we are sending our version of the data back
    // to the originating element.
    switch (storage & (HasQDate | HasQTime)) {
    case HasQDate:
        return toQDate();
    case HasQTime:
        return toQTime();
    case (HasQDate | HasQTime):
        return toQDateTime();
    default:
        return QVariant();
    }
}

QDateTime DateObject::toQDateTime() const
{
    if (d()->isAttachedToProperty())
        d()->readReference();
    return d()->toQDateTime();
}

QString DateObject::toString() const
{
    if (d()->isAttachedToProperty())
        d()->readReference();
    return ToString(d()->date(), engine()->localTZA);
}

QString DateObject::dateTimeToString(const QDateTime &dateTime, ExecutionEngine *engine)
{
    if (!dateTime.isValid())
        return QStringLiteral("Invalid Date");
    return ToString(TimeClip(dateTime.toMSecsSinceEpoch()), engine->localTZA);
}

double DateObject::dateTimeToNumber(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return qQNaN();
    return TimeClip(dateTime.toMSecsSinceEpoch());
}

QDateTime DateObject::stringToDateTime(const QString &string, ExecutionEngine *engine)
{
    return ToDateTime(ParseString(string, engine->localTZA), QTimeZone::LocalTime);
}

QDateTime DateObject::timestampToDateTime(double timestamp, QTimeZone zone)
{
    return ToDateTime(timestamp, zone);
}

double DateObject::componentsToTimestamp(
        double year, double month, double day, double hours,
        double mins, double secs, double ms, ExecutionEngine *v4)
{
    if (year >= 0 && year <= 99)
        year += 1900;
    const double t = MakeDate(MakeDay(year, month, day), MakeTime(hours, mins, secs, ms));
    return UTC(t, v4->localTZA);
}

QDate DateObject::dateTimeToDate(const QDateTime &dateTime)
{
    // If the Date object was parse()d from a string with no time part
    // or zone specifier it's really the UTC start of the relevant day,
    // but it's here represented as a local time, which may fall in the
    // preceding day. See QTBUG-92466 for the gory details.
    const auto utc = dateTime.toUTC();
    if (utc.date() != dateTime.date() && utc.addSecs(-1).date() == dateTime.date())
        return utc.date();

    // This may, of course, be The Wrong Thing if the date was
    // constructed as a full local date-time that happens to coincide
    // with the start of a UTC day; however, that would be an odd value
    // to give to something that, apparently, someone thinks belongs in
    // a QDate.
    return dateTime.date();
}

DEFINE_OBJECT_VTABLE(DateCtor);

Heap::DateObject *Heap::DateObject::detached() const
{
    return internalClass->engine->memoryManager->allocate<QV4::DateObject>(m_date);
}

bool Heap::DateObject::setVariant(const QVariant &variant)
{
    const QMetaType variantReferenceType = variant.metaType();
    switch (variantReferenceType.id()) {
    case QMetaType::Double:
        m_date.init(*static_cast<const double *>(variant.constData()));
        break;
    case QMetaType::QDate:
        m_date.init(*static_cast<const QDate *>(variant.constData()));
        break;
    case QMetaType::QTime:
        m_date.init(*static_cast<const QTime *>(variant.constData()), internalClass->engine);
        break;
    case QMetaType::QDateTime:
        m_date.init(*static_cast<const QDateTime *>(variant.constData()));
        break;
    default:
        return false;
    }

    return true;
}

void Heap::DateCtor::init(QV4::ExecutionEngine *engine)
{
    Heap::FunctionObject::init(engine, QStringLiteral("Date"));
}

ReturnedValue DateCtor::virtualCallAsConstructor(const FunctionObject *that, const Value *argv, int argc, const Value *newTarget)
{
    ExecutionEngine *v4 = that->engine();
    double t = 0;

    if (argc == 0)
        t = currentTime();

    else if (argc == 1) {
        Scope scope(v4);
        ScopedValue arg(scope, argv[0]);
        if (DateObject *d = arg->as<DateObject>()) {
            t = d->date();
        } else {
            arg = RuntimeHelpers::toPrimitive(arg, PREFERREDTYPE_HINT);

            if (String *s = arg->stringValue())
                t = ParseString(s->toQString(), v4->localTZA);
            else
                t = arg->toNumber();
        }
    }

    else { // d.argc > 1
        const double year  = argv[0].toNumber();
        const double month = argv[1].toNumber();
        const double day  = argc >= 3 ? argv[2].toNumber() : 1;
        const double hours = argc >= 4 ? argv[3].toNumber() : 0;
        const double mins = argc >= 5 ? argv[4].toNumber() : 0;
        const double secs = argc >= 6 ? argv[5].toNumber() : 0;
        const double ms    = argc >= 7 ? argv[6].toNumber() : 0;
        t = DateObject::componentsToTimestamp(year, month, day, hours, mins, secs, ms, v4);
    }

    ReturnedValue o = Encode(v4->newDateObject(t));
    if (!newTarget)
        return o;
    Scope scope(v4);
    ScopedObject obj(scope, o);
    obj->setProtoFromNewTarget(newTarget);
    return obj->asReturnedValue();
}

ReturnedValue DateCtor::virtualCall(const FunctionObject *m, const Value *, const Value *, int)
{
    ExecutionEngine *e = m->engine();
    double t = currentTime();
    return e->newString(ToString(t, e->localTZA))->asReturnedValue();
}

void DatePrototype::init(ExecutionEngine *engine, Object *ctor)
{
    Scope scope(engine);
    ScopedObject o(scope);
    ctor->defineReadonlyProperty(engine->id_prototype(), (o = this));
    ctor->defineReadonlyConfigurableProperty(engine->id_length(), Value::fromInt32(7));
    engine->localTZA = getLocalTZA();

    ctor->defineDefaultProperty(QStringLiteral("parse"), method_parse, 1);
    ctor->defineDefaultProperty(QStringLiteral("UTC"), method_UTC, 7);
    ctor->defineDefaultProperty(QStringLiteral("now"), method_now, 0);

    defineDefaultProperty(QStringLiteral("constructor"), (o = ctor));
    defineDefaultProperty(engine->id_toString(), method_toString, 0);
    defineDefaultProperty(QStringLiteral("toDateString"), method_toDateString, 0);
    defineDefaultProperty(QStringLiteral("toTimeString"), method_toTimeString, 0);
    defineDefaultProperty(engine->id_toLocaleString(), method_toLocaleString, 0);
    defineDefaultProperty(QStringLiteral("toLocaleDateString"), method_toLocaleDateString, 0);
    defineDefaultProperty(QStringLiteral("toLocaleTimeString"), method_toLocaleTimeString, 0);
    defineDefaultProperty(engine->id_valueOf(), method_valueOf, 0);
    defineDefaultProperty(QStringLiteral("getTime"), method_getTime, 0);
    defineDefaultProperty(QStringLiteral("getYear"), method_getYear, 0);
    defineDefaultProperty(QStringLiteral("getFullYear"), method_getFullYear, 0);
    defineDefaultProperty(QStringLiteral("getUTCFullYear"), method_getUTCFullYear, 0);
    defineDefaultProperty(QStringLiteral("getMonth"), method_getMonth, 0);
    defineDefaultProperty(QStringLiteral("getUTCMonth"), method_getUTCMonth, 0);
    defineDefaultProperty(QStringLiteral("getDate"), method_getDate, 0);
    defineDefaultProperty(QStringLiteral("getUTCDate"), method_getUTCDate, 0);
    defineDefaultProperty(QStringLiteral("getDay"), method_getDay, 0);
    defineDefaultProperty(QStringLiteral("getUTCDay"), method_getUTCDay, 0);
    defineDefaultProperty(QStringLiteral("getHours"), method_getHours, 0);
    defineDefaultProperty(QStringLiteral("getUTCHours"), method_getUTCHours, 0);
    defineDefaultProperty(QStringLiteral("getMinutes"), method_getMinutes, 0);
    defineDefaultProperty(QStringLiteral("getUTCMinutes"), method_getUTCMinutes, 0);
    defineDefaultProperty(QStringLiteral("getSeconds"), method_getSeconds, 0);
    defineDefaultProperty(QStringLiteral("getUTCSeconds"), method_getUTCSeconds, 0);
    defineDefaultProperty(QStringLiteral("getMilliseconds"), method_getMilliseconds, 0);
    defineDefaultProperty(QStringLiteral("getUTCMilliseconds"), method_getUTCMilliseconds, 0);
    defineDefaultProperty(QStringLiteral("getTimezoneOffset"), method_getTimezoneOffset, 0);
    defineDefaultProperty(QStringLiteral("setTime"), method_setTime, 1);
    defineDefaultProperty(QStringLiteral("setMilliseconds"), method_setMilliseconds, 1);
    defineDefaultProperty(QStringLiteral("setUTCMilliseconds"), method_setUTCMilliseconds, 1);
    defineDefaultProperty(QStringLiteral("setSeconds"), method_setSeconds, 2);
    defineDefaultProperty(QStringLiteral("setUTCSeconds"), method_setUTCSeconds, 2);
    defineDefaultProperty(QStringLiteral("setMinutes"), method_setMinutes, 3);
    defineDefaultProperty(QStringLiteral("setUTCMinutes"), method_setUTCMinutes, 3);
    defineDefaultProperty(QStringLiteral("setHours"), method_setHours, 4);
    defineDefaultProperty(QStringLiteral("setUTCHours"), method_setUTCHours, 4);
    defineDefaultProperty(QStringLiteral("setDate"), method_setDate, 1);
    defineDefaultProperty(QStringLiteral("setUTCDate"), method_setUTCDate, 1);
    defineDefaultProperty(QStringLiteral("setMonth"), method_setMonth, 2);
    defineDefaultProperty(QStringLiteral("setUTCMonth"), method_setUTCMonth, 2);
    defineDefaultProperty(QStringLiteral("setYear"), method_setYear, 1);
    defineDefaultProperty(QStringLiteral("setFullYear"), method_setFullYear, 3);
    defineDefaultProperty(QStringLiteral("setUTCFullYear"), method_setUTCFullYear, 3);

    // ES6: B.2.4.3 & 20.3.4.43:
    // We have to use the *same object* for toUTCString and toGMTString
    {
        QString toUtcString(QStringLiteral("toUTCString"));
        QString toGmtString(QStringLiteral("toGMTString"));
        ScopedString us(scope, engine->newIdentifier(toUtcString));
        ScopedString gs(scope, engine->newIdentifier(toGmtString));
        ScopedFunctionObject toUtcGmtStringFn(scope, FunctionObject::createBuiltinFunction(engine, us, method_toUTCString, 0));
        defineDefaultProperty(us, toUtcGmtStringFn);
        defineDefaultProperty(gs, toUtcGmtStringFn);
    }

    defineDefaultProperty(QStringLiteral("toISOString"), method_toISOString, 0);
    defineDefaultProperty(QStringLiteral("toJSON"), method_toJSON, 1);
    defineDefaultProperty(engine->symbol_toPrimitive(), method_symbolToPrimitive, 1, Attr_ReadOnly_ButConfigurable);
}

double DatePrototype::getThisDate(ExecutionEngine *v4, const Value *thisObject)
{
    if (const DateObject *that = thisObject->as<DateObject>()) {
        if (that->d()->isAttachedToProperty())
            that->d()->readReference();

        return that->date();
    }
    v4->throwTypeError();
    return 0;
}

ReturnedValue DatePrototype::method_parse(const FunctionObject *f, const Value *, const Value *argv, int argc)
{
    if (!argc)
        return Encode(qt_qnan());
    else
        return Encode(ParseString(argv[0].toQString(), f->engine()->localTZA));
}

ReturnedValue DatePrototype::method_UTC(const FunctionObject *f, const Value *, const Value *argv, int argc)
{
    const int numArgs = argc;
    if (numArgs < 1)
        return Encode(qQNaN());
    ExecutionEngine *e = f->engine();
    double year  = argv[0].toNumber();
    if (e->hasException)
        return Encode::undefined();
    double month = numArgs >= 2 ? argv[1].toNumber() : 0;
    if (e->hasException)
        return Encode::undefined();
    double day   = numArgs >= 3 ? argv[2].toNumber() : 1;
    if (e->hasException)
        return Encode::undefined();
    double hours = numArgs >= 4 ? argv[3].toNumber() : 0;
    if (e->hasException)
        return Encode::undefined();
    double mins  = numArgs >= 5 ? argv[4].toNumber() : 0;
    if (e->hasException)
        return Encode::undefined();
    double secs  = numArgs >= 6 ? argv[5].toNumber() : 0;
    if (e->hasException)
        return Encode::undefined();
    double ms    = numArgs >= 7 ? argv[6].toNumber() : 0;
    if (e->hasException)
        return Encode::undefined();
    double iyear = QV4::Value::toInteger(year);
    if (!qIsNaN(year) && iyear >= 0 && iyear <= 99)
        year = 1900 + iyear;
    double t = MakeDate(MakeDay(year, month, day),
                        MakeTime(hours, mins, secs, ms));
    return Encode(TimeClip(t));
}

ReturnedValue DatePrototype::method_now(const FunctionObject *, const Value *, const Value *, int)
{
    return Encode(currentTime());
}

ReturnedValue DatePrototype::method_toString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(v4->newString(ToString(t, v4->localTZA)));
}

ReturnedValue DatePrototype::method_toDateString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(v4->newString(ToDateString(t)));
}

ReturnedValue DatePrototype::method_toTimeString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(v4->newString(ToTimeString(t)));
}

ReturnedValue DatePrototype::method_toLocaleString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(v4->newString(ToLocaleString(t)));
}

ReturnedValue DatePrototype::method_toLocaleDateString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(v4->newString(ToLocaleDateString(t)));
}

ReturnedValue DatePrototype::method_toLocaleTimeString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(v4->newString(ToLocaleTimeString(t)));
}

ReturnedValue DatePrototype::method_valueOf(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getTime(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getYear(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = YearFromTime(LocalTime(t, v4->localTZA)) - 1900;
    return Encode(t);
}

ReturnedValue DatePrototype::method_getFullYear(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = YearFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCFullYear(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = YearFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getMonth(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = MonthFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCMonth(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = MonthFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getDate(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = DateFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCDate(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = DateFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getDay(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = WeekDay(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCDay(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = WeekDay(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getHours(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = HourFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCHours(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = HourFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getMinutes(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = MinFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCMinutes(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = MinFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getSeconds(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = SecFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCSeconds(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = SecFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getMilliseconds(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = msFromTime(LocalTime(t, v4->localTZA));
    return Encode(t);
}

ReturnedValue DatePrototype::method_getUTCMilliseconds(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = msFromTime(t);
    return Encode(t);
}

ReturnedValue DatePrototype::method_getTimezoneOffset(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    double t = getThisDate(v4, thisObject);
    if (!std::isnan(t))
        t = (t - LocalTime(t, v4->localTZA)) / msPerMinute;
    return Encode(t);
}

ReturnedValue DatePrototype::method_setTime(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setMilliseconds(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    double ms = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    self->setDate(UTC(MakeDate(Day(t), MakeTime(HourFromTime(t), MinFromTime(t), SecFromTime(t), ms)), v4->localTZA));
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCMilliseconds(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double ms = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    self->setDate(MakeDate(Day(t), MakeTime(HourFromTime(t), MinFromTime(t), SecFromTime(t), ms)));
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setSeconds(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    double sec = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double ms = (argc < 2) ? msFromTime(t) : argv[1].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = UTC(MakeDate(Day(t), MakeTime(HourFromTime(t), MinFromTime(t), sec, ms)), v4->localTZA);
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCSeconds(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    double sec = argc ? argv[0].toNumber() : qt_qnan();
    double ms = (argc < 2) ? msFromTime(t) : argv[1].toNumber();
    t = MakeDate(Day(t), MakeTime(HourFromTime(t), MinFromTime(t), sec, ms));
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setMinutes(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    double min = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double sec = (argc < 2) ? SecFromTime(t) : argv[1].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double ms = (argc < 3) ? msFromTime(t) : argv[2].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = UTC(MakeDate(Day(t), MakeTime(HourFromTime(t), min, sec, ms)), v4->localTZA);
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCMinutes(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    double min = argc ? argv[0].toNumber() : qt_qnan();
    double sec = (argc < 2) ? SecFromTime(t) : argv[1].toNumber();
    double ms = (argc < 3) ? msFromTime(t) : argv[2].toNumber();
    t = MakeDate(Day(t), MakeTime(HourFromTime(t), min, sec, ms));
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setHours(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    double hour = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double min = (argc < 2) ? MinFromTime(t) : argv[1].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double sec = (argc < 3) ? SecFromTime(t) : argv[2].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double ms = (argc < 4) ? msFromTime(t) : argv[3].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = UTC(MakeDate(Day(t), MakeTime(hour, min, sec, ms)), v4->localTZA);
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCHours(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    double hour = argc ? argv[0].toNumber() : qt_qnan();
    double min = (argc < 2) ? MinFromTime(t) : argv[1].toNumber();
    double sec = (argc < 3) ? SecFromTime(t) : argv[2].toNumber();
    double ms = (argc < 4) ? msFromTime(t) : argv[3].toNumber();
    t = MakeDate(Day(t), MakeTime(hour, min, sec, ms));
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setDate(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    double date = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = UTC(MakeDate(MakeDay(YearFromTime(t), MonthFromTime(t), date), TimeWithinDay(t)), v4->localTZA);
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCDate(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double date = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = MakeDate(MakeDay(YearFromTime(t), MonthFromTime(t), date), TimeWithinDay(t));
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setMonth(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    double month = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double date = (argc < 2) ? DateFromTime(t) : argv[1].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = UTC(MakeDate(MakeDay(YearFromTime(t), month, date), TimeWithinDay(t)), v4->localTZA);
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCMonth(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    double month = argc ? argv[0].toNumber() : qt_qnan();
    double date = (argc < 2) ? DateFromTime(t) : argv[1].toNumber();
    t = MakeDate(MakeDay(YearFromTime(t), month, date), TimeWithinDay(t));
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setYear(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    if (std::isnan(t))
        t = 0;
    else
        t = LocalTime(t, v4->localTZA);
    double year = argc ? argv[0].toNumber() : qt_qnan();
    double r;
    if (std::isnan(year)) {
        r = qt_qnan();
    } else {
        if ((QV4::Value::toInteger(year) >= 0) && (QV4::Value::toInteger(year) <= 99))
            year += 1900;
        r = MakeDay(year, MonthFromTime(t), DateFromTime(t));
        r = UTC(MakeDate(r, TimeWithinDay(t)), v4->localTZA);
    }
    self->setDate(r);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setUTCFullYear(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = self->date();
    double year = argc ? argv[0].toNumber() : qt_qnan();
    double month = (argc < 2) ? MonthFromTime(t) : argv[1].toNumber();
    double date = (argc < 3) ? DateFromTime(t) : argv[2].toNumber();
    t = MakeDate(MakeDay(year, month, date), TimeWithinDay(t));
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_setFullYear(const FunctionObject *b, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    double t = LocalTime(self->date(), v4->localTZA);
    if (v4->hasException)
        return QV4::Encode::undefined();
    if (std::isnan(t))
        t = 0;
    double year = argc ? argv[0].toNumber() : qt_qnan();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double month = (argc < 2) ? MonthFromTime(t) : argv[1].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    double date = (argc < 3) ? DateFromTime(t) : argv[2].toNumber();
    if (v4->hasException)
        return QV4::Encode::undefined();
    t = UTC(MakeDate(MakeDay(year, month, date), TimeWithinDay(t)), v4->localTZA);
    self->setDate(t);
    return Encode(self->date());
}

ReturnedValue DatePrototype::method_toUTCString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    if (self->d()->isAttachedToProperty())
        self->d()->readReference();

    double t = self->date();
    return Encode(v4->newString(ToUTCString(t)));
}

static void addZeroPrefixedInt(QString &str, int num, int nDigits)
{
    str.resize(str.size() + nDigits);

    QChar *c = str.data() + str.size() - 1;
    while (nDigits) {
        *c = QChar(num % 10 + '0');
        num /= 10;
        --c;
        --nDigits;
    }
}

ReturnedValue DatePrototype::method_toISOString(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    DateObject *self = const_cast<DateObject *>(thisObject->as<DateObject>());
    if (!self)
        return v4->throwTypeError();

    if (self->d()->isAttachedToProperty())
        self->d()->readReference();

    double t = self->date();
    if (!std::isfinite(t))
        RETURN_RESULT(v4->throwRangeError(*thisObject));

    QString result;
    int year = (int)YearFromTime(t);
    if (year < 0 || year > 9999) {
        if (qAbs(year) >= 1000000)
            RETURN_RESULT(v4->throwRangeError(*thisObject));
        result += year < 0 ? QLatin1Char('-') : QLatin1Char('+');
        year = qAbs(year);
        addZeroPrefixedInt(result, year, 6);
    } else {
        addZeroPrefixedInt(result, year, 4);
    }
    result += QLatin1Char('-');
    addZeroPrefixedInt(result, (int)MonthFromTime(t) + 1, 2);
    result += QLatin1Char('-');
    addZeroPrefixedInt(result, (int)DateFromTime(t), 2);
    result += QLatin1Char('T');
    addZeroPrefixedInt(result, HourFromTime(t), 2);
    result += QLatin1Char(':');
    addZeroPrefixedInt(result, MinFromTime(t), 2);
    result += QLatin1Char(':');
    addZeroPrefixedInt(result, SecFromTime(t), 2);
    result += QLatin1Char('.');
    addZeroPrefixedInt(result, msFromTime(t), 3);
    result += QLatin1Char('Z');

    return Encode(v4->newString(result));
}

ReturnedValue DatePrototype::method_toJSON(const FunctionObject *b, const Value *thisObject, const Value *, int)
{
    ExecutionEngine *v4 = b->engine();
    Scope scope(v4);
    ScopedObject O(scope, thisObject->toObject(v4));
    if (v4->hasException)
        return QV4::Encode::undefined();

    ScopedValue tv(scope, RuntimeHelpers::toPrimitive(O, NUMBER_HINT));

    if (tv->isNumber() && !std::isfinite(tv->toNumber()))
        return Encode::null();

    ScopedString s(scope, v4->newString(QStringLiteral("toISOString")));
    ScopedValue v(scope, O->get(s));
    FunctionObject *toIso = v->as<FunctionObject>();

    if (!toIso)
        return v4->throwTypeError();

    return checkedResult(v4, toIso->call(O, nullptr, 0));
}

ReturnedValue DatePrototype::method_symbolToPrimitive(const FunctionObject *f, const Value *thisObject, const Value *argv, int argc)
{
    ExecutionEngine *e = f->engine();
    if (!thisObject->isObject() || !argc || !argv->isString())
        return e->throwTypeError();

    String *hint = argv->stringValue();
    PropertyKey id = hint->toPropertyKey();
    if (id == e->id_default()->propertyKey())
        hint = e->id_string();
    else if (id != e->id_string()->propertyKey() && id != e->id_number()->propertyKey())
        return e->throwTypeError();

    return RuntimeHelpers::ordinaryToPrimitive(e, static_cast<const Object *>(thisObject), hint);
}

void DatePrototype::timezoneUpdated(ExecutionEngine *e)
{
    e->localTZA = getLocalTZA();
}
