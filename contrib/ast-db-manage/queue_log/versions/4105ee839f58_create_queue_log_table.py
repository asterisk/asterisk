"""create queue_log table

Revision ID: 4105ee839f58
Revises:
Create Date: 2016-09-30 22:32:45.918340

"""

# revision identifiers, used by Alembic.
revision = '4105ee839f58'
down_revision = None
branch_labels = None
depends_on = None

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.create_table(
        'queue_log',
        sa.Column('id', sa.BigInteger, primary_key=True, nullable=False,
                  unique=True, autoincrement=True),
        sa.Column('time', sa.DateTime()),
        sa.Column('callid', sa.String(80)),
        sa.Column('queuename', sa.String(256)),
        sa.Column('agent', sa.String(80)),
        sa.Column('event', sa.String(32)),
        sa.Column('data1', sa.String(100)),
        sa.Column('data2', sa.String(100)),
        sa.Column('data3', sa.String(100)),
        sa.Column('data4', sa.String(100)),
        sa.Column('data5', sa.String(100))
    )


def downgrade():
    op.drop_table('queue_log')
