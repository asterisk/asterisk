"""increase queue_log time precision

Revision ID: 8e6a9c2f4b17
Revises: 4105ee839f58
Create Date: 2026-08-03 00:00:00.000000

"""

# revision identifiers, used by Alembic.
revision = "8e6a9c2f4b17"
down_revision = "4105ee839f58"

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.mysql import DATETIME as MYSQL_DATETIME


def upgrade():
    # MariaDB is reported as the 'mysql' dialect, so this covers both.
    if op.get_context().bind.dialect.name == "mysql":
        op.alter_column(
            "queue_log",
            "time",
            existing_type=sa.DateTime(),
            type_=MYSQL_DATETIME(fsp=6),
            existing_nullable=True,
        )


def downgrade():
    if op.get_context().bind.dialect.name == "mysql":
        op.alter_column(
            "queue_log",
            "time",
            existing_type=MYSQL_DATETIME(fsp=6),
            type_=sa.DateTime(),
            existing_nullable=True,
        )
